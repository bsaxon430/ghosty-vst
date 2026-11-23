# SPECTRE (build 17): the page Ghost Lab opens on

SPECTRE listens to the sound, finds up to 1,024 of its sine-wave partials, and plays it back from a bank of oscillators, so each partial can be bent on the way out. It is live: in **Ghost Lab FX**, **LIVE ON THIS TRACK** runs the track's own audio through it as FL plays (the plug-in reports SPECTRE's latency, about 43 ms, so FL lines the track up). In **Ghost Lab** (the instrument), **LISTEN** plays your sound round and round through it, and so does **a held key**: while the SPECTRE (or SPACE) page is open, every note you play goes through it live and keeps going round while the key is held, so every move of a control is heard at once. **PRINT IT** renders the whole sound through it into the lab's history (UNDO takes it back; printing again replaces the last print). **RESET** puts every control back. Double-click any control for its start value; hold shift while dragging for fine moves. Every control is also a plug-in parameter ("SPECTRE PHASE" ...), so FL can automate it.

SPECTRE splits the sound three ways: the **partials** (sine waves, which every control bends), the **noise** that is left (breath, hiss, the body of a drum, played back as shaped noise), and the **attacks** (the first few milliseconds of each hit, let through as they were).

| Control | Range | What it does |
|---|---|---|
| PHASE (pad, across) | -100 .. +100 | left locks every partial's phase together (a robot buzz); right scatters them (a whisper) |
| CEPSTRUM (pad, up / down) | -100 .. +100 | up keeps only the body of the sound (its spectral envelope: the vowel, the formants); down keeps only the fine detail (all partials equal, bright and raw) |
| SHIFT | -1000 .. +1000 Hz | slides every partial by the same number of Hz: inharmonic, metallic |
| TRANSPOSE | -24 .. +24 st | moves the notes; the body stays where it was, so a voice still sounds like a voice |
| CLOUD | 0 .. 100% | partials ring on (up to ~6 s) and drift apart (up to 30 cents): a wash |
| SMOOTH | 0 .. 100% | every change glides (up to ~1 s) |
| MID / SIDE | 0 .. 100% | how much of the middle / the stereo sides is rebuilt (the rest is the dry sound, lined up). SIDE is greyed on a mono sound |
| INF | 0 .. 100% | hold: the higher, the less of each new moment gets in; at the top the sound freezes forever (CLOUD and SMOOTH grey out: nothing moves) |
| PARTIALS | 16 .. 1024 | how many sine waves rebuild it (the loudest ones are kept) |
| OUTPUT | -24 .. +12 dB | the level; a safety limiter keeps it under 0 dBFS |
| NOISE | 0 .. 100% | up: hiss, breath and drums stay noise (a peak must stand clear of its neighbours to count as a sine wave); down: everything becomes sine waves, the glassy, tuned sound |
| TRANSIENT | 0 .. 100% | lets the first few milliseconds of every hit through as it was, so drums keep their punch (greyed while INF holds everything) |
| WAVETABLE (the box over the pad) | SINE, SAW, SQUARE, TRIANGLE, PULSE, ORGAN, GLASS, GHOST, FM, or your own | each partial plays this shape instead of a sine wave (band-limited: nothing folds back past Nyquist). The folder, or a WAV dropped on the box, loads your own: a single cycle, or a Serum / Vital wavetable file (its first frame). Saved with the session and with PRINT IT |

# Ghost Lab UNDER THE HOOD: every CDP control, and where it is now

Ghost Lab runs the real CDP8 programs (`pvoc stretch strange focus blur modify combine morph`, plus `repitch formants envel` for the data files combine make / make2 need, all unmodified, pinned at commit 28bc42c7, see `cdp/CDP-PINNED.txt`), plus CDP's own `cdparams` program: the one CDP's own front end (the Sound Loom) asks for the parameter page of a process. UNDER THE HOOD lists every process of those programs, grouped the way CDP groups them (by program), and shows each one's **ORIGINAL page**: every control exactly as cdparams defines it for that process and mode (CDP's own control name, its kind: slider / number box / check button / data file, its range, its default, the slider's starting sub-range, whether it may be time-varying), in our colours, with our plain words printed small under CDP's name. What you set goes to the program exactly as it is: no scaling, no clamping, no hidden processing. RUN shows the command line(s) that ran and CDP's own words. The result is the processed sound (CDP's output as it is: no gain, no fades), and UNDO brings back the sound before it.

Nothing here is typed in by hand: `cdp/gen-hood-catalog.py` keeps each program's usage text (verbatim; INFORMATION on the page shows it) and reads the command words from its USAGE lines; `cdp/gen-hood-dialogs.py` asks cdparams for every process and mode (process numbers from CDP's `processno.h` and the programs' own name tables, mode counts from CDP's `get_maxmode`, option letters from CDP's own `set_vflgs` table) and keeps what it prints. The tests check that every page is byte for byte what the shipped cdparams prints, and that runs from the page are bit-exact with the same CDP commands typed by hand.

## Coverage

- **54 processes**: every process of the 8 sound programs, plus the 4 data-file processes of `repitch` (getpitch, analenv), `formants` (get) and `envel` (extract); **88 modes**, **121 ORIGINAL pages** (one per process and mode)
- **372 controls** on those pages (129 may be time-varying breakpoint files, 48 check buttons); **95 usage forms**, **284 command words**
- `modify loudness`: CDP's table has 12 modes but no process for 9 and 10 (its usage text lists 1-8, 11, 12), so those two are not pages.
- Ghost Lab FX (the mixer-insert version) has the same UNDER THE HOOD page: every process renders on the captured track sound; none is left out.

## How a page works

- **Mode**: the list at the top, CDP's own mode words (from its usage text). The page below changes with it.
- **Slider controls** (LINEAR / LOG / PLOG / POWTWO) open on CDP's starting sub-range; **FULL RANGE** opens them to everything CDP accepts; a typed value outside the starting range opens it too. The number box and the slider are the same value.
- **Number boxes** (NUMERIC / LOGNUMERIC): CDP's front end shows these as entries, so the page does too, with the range beside them.
- **DEFAULT** puts CDP's own default back for that control; **SET DEFAULTS** for the whole page. A page opens at CDP's defaults.
- **BRK** (on controls whose CDP type is D or I): time-varying. The control becomes a text box for time value pairs (`0 1.5  2 0.7`) or the path of a breakpoint file; Ghost Lab writes the pairs to a file and gives CDP its name, as CDP expects.
- **FORMANT_BANDS** (strange glis, focus focus, blur spread): CDP's PITCHWISE / FRQWISE choice with its own range for each, sent as `-pN` / `-fN`; FORMANT_QUICKSEARCH is CDP's `-i`.
- **Optional flags with a value** (`-r`, `-l`...): the box shows CDP's default and **SEND** is off, so CDP behaves as without the flag; tick SEND to send the value.
- **Check buttons** are CDP's bare flags (`-x`), sent when ticked. A few are in CDP's own table but not in its usage text (brassage `-d`); they are on the page and marked.
- **Data files** (FILENAME / STRING): type the data or paste a path, drop a file on the field, or **PICK** (a file made in this session, or browse).
- **MAKE FROM SOUND** (build 8) sits beside every pitch / formant / envelope file field (`combine make`, `combine make2`): it runs CDP's own extractor on the current sound or SPECIMEN 002 and fills the field. Pitch data = `pvoc anal` + `repitch getpitch 1` (its binary pitch file, .frq); formant data = `pvoc anal` + `formants get -p4` (.for); envelope data = `pvoc anal` + `repitch analenv` (.evl, the window-loudness envelope of the analysis, which is what make2 wants). The same analysis settings (POINTS / OVERLAP) are used throughout, as make needs. The files live in the session's data folder; the page shows what was made and the commands.
- **The data-file processes' own pages** (`repitch getpitch`, `repitch analenv`, `formants get`, `envel extract`) run like any other: the sound stays (getpitch's detected-pitch tone is heard), the data file is kept in the session's data folder and listed under the command (`# DATA frq: ...`), so PICK can use it.
- **Several sounds** (`modify sausage`, and `combine interleave` / `max`, `modify loudness 7-8`): a MORE SOUNDS box on the page: + CURRENT SOUND, + SPECIMEN 002, + EARLIER RESULT (any step of the history), + FILE, or drop files on it. The current sound is always the first input; the others follow in the order listed. `modify loudness 7` reports which file is loudest; `8` writes one file per input, so only the first is heard.
- **Ranges follow the sound**: the baked pages were asked for a 10 s mono 44100 Hz sound analysed with 1024 points / overlap 3 (CDP's defaults). With a sound loaded, Ghost Lab asks the shipped cdparams again with the real sound (length, rate, channels, your POINTS / OVERLAP), so limits like a start time's ceiling or a blur's window count are the real ones; the header says which.
- **Spectral programs** (`blur combine focus morph strange stretch`) work on an analysis, so CDP's own `pvoc anal 1 in.wav in.ana -cPOINTS -oOVERLAP` runs first and `pvoc synth` after. Stereo sounds run once per channel. `modify` programs read the sound as it is. Two-sound processes use the specimen and SPECIMEN 002.
- **Text answers** (`stretch time 2`, `modify speed 3/4`, `modify findpan`, `modify spaceform`) leave the sound as it is and show CDP's answer under the command.
- **Engine call:** `Hood.cpp: renderHood` -> `Cdp::run`; the pages: `hood::dialogs()` / `hood::liveDialog` (cdparams) and `HoodPage.h`; the step is saved with the session (`Op::Hood`).

### Where CDP's own table and its usage text disagree

- `modify radical` mode 3: option 1 (LOWEST_DOWNWARD_TRANSPOSITION) is -h in CDP's own table but -l in the usage line: the table decides
- `modify radical` mode 3: option 2 (HIGHEST_UPWARD_TRANSPOSITION) is -l in CDP's own table but -h in the usage line: the table decides

## The ORIGINAL pages

Columns: CDP's control (its own name, from cdparams), its kind, type (i / d fixed int / number; I / D may be a breakpoint file), range, default, the slider's starting sub-range, our plain words (the command word it drives, with the first words of CDP's own help), and the command argument.

## pvoc  (SPECTRAL ANALYSIS AND RESYNTHESIS)

### pvoc anal: CONVERT SOUNDFILE TO SPECTRAL FILE

**Mode 1**: STANDARD ANALYSIS  (`pvoc anal 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| ANALYSIS_POINTS | slider (powers of 2) | i | 2 to 32768 | 1024 |  | `points`  No of analysis points (2-32768 (power of 2)): default 1024 | `-cVALUE` when SEND is ticked |
| ANALWINDOW_OVERLAP | number box | i | 1 to 4 | 3 |  | `overlap`  Filter overlap factor (1-4): default 3 | `-oVALUE` when SEND is ticked |

**Mode 2**: OUTPUT SPECTRAL ENVELOPE VALS ONLY  (`pvoc anal 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| ANALYSIS_POINTS | slider (powers of 2) | i | 2 to 32768 | 1024 |  | `points`  No of analysis points (2-32768 (power of 2)): default 1024 | `-cVALUE` when SEND is ticked |
| ANALWINDOW_OVERLAP | number box | i | 1 to 4 | 3 |  | `overlap`  Filter overlap factor (1-4): default 3 | `-oVALUE` when SEND is ticked |

**Mode 3**: OUTPUT SPECTRAL MAGNITUDE VALS ONLY  (`pvoc anal 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| ANALYSIS_POINTS | slider (powers of 2) | i | 2 to 32768 | 1024 |  | `points`  No of analysis points (2-32768 (power of 2)): default 1024 | `-cVALUE` when SEND is ticked |
| ANALWINDOW_OVERLAP | number box | i | 1 to 4 | 3 |  | `overlap`  Filter overlap factor (1-4): default 3 | `-oVALUE` when SEND is ticked |

### pvoc synth: CONVERT SPECTRAL FILE TO SOUNDFILE

**No modes**  (`pvoc synth ...`)

No settings of its own: RUN runs it as it is.

### pvoc extract: ANALYSE THEN RESYNTHESIZE SOUND WITH VARIOUS OPTIONS

**No modes**  (`pvoc extract ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| ANALYSIS_POINTS | slider (log) | i | 2 to 32768 | 1024 |  | `points`  No of analysis points (2-32768 (power of 2)): default 1024 | `-cVALUE` when SEND is ticked |
| ANALWINDOW_OVERLAP | number box | i | 1 to 4 | 3 |  | `overlap`  Filter overlap factor (1-4): default 3 | `-oVALUE` when SEND is ticked |
| SELECT_A_CHANNEL | number box | i | 0 to 2 | 0 |  | `dochans`  resynthesize odd (1) or even (2) channels only. | `-dVALUE` when SEND is ticked |
| BOTTOM_RESYNTH_CHANNEL | slider | i | 0 to 16384 | 0 | 0 to 1024 | `lochan`  ignore analysis channels below this in resynth (default: 0) | `-lVALUE` when SEND is ticked |
| TOP_RESYNTH_CHANNEL | slider | i | 0 to 16384 | 0 | 0 to 1024 | `hichan`  ignore analysis channels above this in resynth (dflt: highest channel) | `-hVALUE` when SEND is ticked |

## stretch  (STRETCHING A SPECTRAL FILE)

### stretch spectrum: STRETCH THE FREQUENCIES IN THE SPECTRUM

**Mode 1**: Stretch above the frq_divide.  (`stretch spectrum 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ_DIVIDE | slider (pitch-log) | d | 5 to 22050 | 440 |  | `frq_divide` | as set |
| TOP_OF_SPECTRUM_STRETCH | slider (log) | d | 0.000454 to 2205 | 1.4 | 0.25 to 4 | `maxstretch` | as set |
| STRETCH_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `exponent`  specifies the type of stretching required. (> 0) | as set |
| PROCESS_DEPTH | slider | D | 0 to 1 | 1 |  | `depth`  stretch effect on source (from 0 (no effect) to 1 (full effect)) | `-dVALUE` when SEND is ticked |

**Mode 2**: Stretch below the frq_divide.  (`stretch spectrum 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ_DIVIDE | slider (pitch-log) | d | 5 to 22050 | 440 |  | `frq_divide` | as set |
| TOP_OF_SPECTRUM_STRETCH | slider (log) | d | 0.000454 to 2205 | 1.4 | 0.25 to 4 | `maxstretch` | as set |
| STRETCH_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `exponent`  specifies the type of stretching required. (> 0) | as set |
| PROCESS_DEPTH | slider | D | 0 to 1 | 1 |  | `depth`  stretch effect on source (from 0 (no effect) to 1 (full effect)) | `-dVALUE` when SEND is ticked |

### stretch time: TIME-STRETCHING OF INFILE.

**Mode 1**  (`stretch time 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESTRETCH_MULTIPLIER | slider (log) | D | 0.0001 to 10000 | 1 | 0.0625 to 16 | `timestretch` | as set (a file name when BRK) |

**Mode 2**  (`stretch time 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESTRETCH_MULTIPLIER | slider (log) | D | 0.0001 to 10000 | 1 | 0.0625 to 16 | `timestretch` | as set (a file name when BRK) |

## strange  (STRANGE OPERATIONS ON A SPECTRAL FILE)

### strange shift: LINEAR FREQUENCY SHIFT OF (PART OF) THE SPECTRUM

**Mode 1**: Shift the whole spectrum.  (`strange shift 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LINEAR_FRQ_SHIFT | slider | D | -22050 to 22050 | 0 | -200 to 200 | `frqshift`  linear shift of spectral frequencies (same for all). | as set (a file name when BRK) |
| LOG_INTERP_CHANGING_FRQ | check button |  |  |  |  | `-l` | `-l` when ticked |

**Mode 2**: Shift the spectrum above frq_divide.  (`strange shift 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LINEAR_FRQ_SHIFT | slider | D | -22050 to 22050 | 0 | -200 to 200 | `frqshift`  linear shift of spectral frequencies (same for all). | as set (a file name when BRK) |
| FRQ_DIVIDE | slider (pitch-log) | D | 10.766602 to 22039.233398 | 10.766602 |  | `frq_divide`  frq at which shifting starts or stops. | as set (a file name when BRK) |
| LOG_INTERP_CHANGING_FRQ | check button |  |  |  |  | `-l` | `-l` when ticked |

**Mode 3**: Shift the spectrum below frq_divide.  (`strange shift 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LINEAR_FRQ_SHIFT | slider | D | -22050 to 22050 | 0 | -200 to 200 | `frqshift`  linear shift of spectral frequencies (same for all). | as set (a file name when BRK) |
| FRQ_DIVIDE | slider (pitch-log) | D | 10.766602 to 22039.233398 | 10.766602 |  | `frq_divide`  frq at which shifting starts or stops. | as set (a file name when BRK) |
| LOG_INTERP_CHANGING_FRQ | check button |  |  |  |  | `-l` | `-l` when ticked |

**Mode 4**: Shift the spectrum only in the range frqlo and frqhi.  (`strange shift 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LINEAR_FRQ_SHIFT | slider | D | -22050 to 22050 | 0 | -200 to 200 | `frqshift`  linear shift of spectral frequencies (same for all). | as set (a file name when BRK) |
| LOW_FRQ | slider (pitch-log) | D | 10.766602 to 22039.233398 | 10.766602 |  | `frqlo` | as set (a file name when BRK) |
| HIGH_FRQ | slider (pitch-log) | D | 10.766602 to 22039.233398 | 22039.233398 |  | `frqhi` | as set (a file name when BRK) |
| LOG_INTERP_CHANGING_FRQ | check button |  |  |  |  | `-l` | `-l` when ticked |

**Mode 5**: Shift the spectrum outside the range  frqlo to frqhi.  (`strange shift 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LINEAR_FRQ_SHIFT | slider | D | -22050 to 22050 | 0 | -200 to 200 | `frqshift`  linear shift of spectral frequencies (same for all). | as set (a file name when BRK) |
| LOW_FRQ | slider (pitch-log) | D | 10.766602 to 22039.233398 | 10.766602 |  | `frqlo` | as set (a file name when BRK) |
| HIGH_FRQ | slider (pitch-log) | D | 10.766602 to 22039.233398 | 22039.233398 |  | `frqhi` | as set (a file name when BRK) |
| LOG_INTERP_CHANGING_FRQ | check button |  |  |  |  | `-l` | `-l` when ticked |

### strange glis: CREATE GLISSANDI INSIDE THE (CHANGING) SPECTRAL ENVELOPE OF ORIGINAL SOUND

**Mode 1**: shepard tones.  (`strange glis 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |
| GLISS_RATE | slider | D | -1034.604171 to 1034.604171 | 1 | -4 to 4 | `glisrate` | as set (a file name when BRK) |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22050 | 22050 |  | `topfrq`  top of spectrum: must be > 2*chanwidth of analysis (default:nyquist) | `-tVALUE` when SEND is ticked |

**Mode 2**: inharmonic glide.  (`strange glis 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |
| GLISS_RATE | slider | D | -1034.604171 to 1034.604171 | 1 | -4 to 4 | `glisrate` | as set (a file name when BRK) |
| PARTIAL_SPACING_(HZ) | slider | d | 43.066406 to 11025 | 50 | 43.066406 to 200 | `hzstep`  partials-spacing in inharmonic glide. | as set |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22050 | 22050 |  | `topfrq`  top of spectrum: must be > 2*chanwidth of analysis (default:nyquist) | `-tVALUE` when SEND is ticked |

**Mode 3**: self-glissando.  (`strange glis 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |
| GLISS_RATE | slider | D | -1034.604171 to 1034.604171 | 1 | -4 to 4 | `glisrate` | as set (a file name when BRK) |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22050 | 22050 |  | `topfrq`  top of spectrum: must be > 2*chanwidth of analysis (default:nyquist) | `-tVALUE` when SEND is ticked |

### strange waver: OSCILLATE BETWEEN HARMONIC AND INHARMONIC STATE

**Mode 1**: Standard spectral stretching for inharmonic state.  (`strange waver 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| VIBRATO_FRQ | slider | D | 0.10002 to 64.662761 | 8 | 0.10002 to 64 | `vibfrq`  is frq of oscillation. | as set (a file name when BRK) |
| MAX_SPECTRAL_STRETCH | slider | D | 1 to 2205 | 1 | 1 to 4 | `stretch` | as set (a file name when BRK) |
| BASE_FRQ_OF_STRETCH | slider (pitch-log) | d | 5 to 22050 | 5 |  | `botfrq`  is frq above which spectral stretching happens. | as set |

**Mode 2**: Specify spectral stretching for inharmonic state.  (`strange waver 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| VIBRATO_FRQ | slider | D | 0.10002 to 64.662761 | 8 | 0.10002 to 64 | `vibfrq`  is frq of oscillation. | as set (a file name when BRK) |
| MAX_SPECTRAL_STRETCH | slider | D | 1 to 2205 | 1 | 1 to 4 | `stretch` | as set (a file name when BRK) |
| BASE_FRQ_OF_STRETCH | slider (pitch-log) | d | 5 to 22050 | 5 |  | `botfrq`  is frq above which spectral stretching happens. | as set |
| EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `expon`  defines type of stretch (must be > 0.0). | as set |

### strange invert: INVERT THE SPECTRUM

**Mode 1**: Normal inversion.  (`strange invert 1 ...`)

No settings of its own: RUN runs it as it is.

**Mode 2**: Output sound retains amplitude envelope of source sound.  (`strange invert 2 ...`)

No settings of its own: RUN runs it as it is.

## focus  (FOCUSING OPERATIONS ON A SPECTRAL FILE)

### focus accu: SUSTAIN EACH SPECTRAL BAND, UNTIL LOUDER DATA APPEARS IN THAT BAND

**No modes**  (`focus accu ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DECAY_RATE | slider | D | 0.001 to 1 | 1 |  | `decay`  sutained channel data decays by factor DECAY per sec. | `-dVALUE` when SEND is ticked |
| GLISS_RATE | slider | D | -8.082845 to 8.082845 | 0 | -1 to 1 | `glis`  sutained channel data glisses at GLIS 8vas per sec. | `-gVALUE` when SEND is ticked |

### focus exag: EXAGGERATE SPECTRAL CONTOUR

**No modes**  (`focus exag ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| EXAGGERATION | slider (log) | D | 0.001 to 1000 | 2 | 0.25 to 4 | `exaggeration` | as set (a file name when BRK) |

### focus focus: FOCUS SPECTRAL ENERGY ONTO PEAKS IN SPECTRUM

**No modes**  (`focus focus ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |
| MAX_PEAKS_TO_FIND | number box | i | 1 to 16 | 16 |  | `pk`  (max) number of peaks to find : Range 1 - 16 | as set |
| FILTER_BANDWIDTH | slider (log) | D | 0.083333 to 10 | 0.333333 |  | `bw`  bandwidth of peak-centred filters, in octaves. | as set (a file name when BRK) |
| BOTTOM_FRQ | slider (pitch-log) | D | 5 to 22050 | 5 |  | `bt`  BT is bottom frequency to start peak search. | `-bVALUE` when SEND is ticked |
| TOP_FRQ | slider (pitch-log) | D | 5 to 22050 | 22050 |  | `tp`  TP is top frequency to end peak search. | `-tVALUE` when SEND is ticked |
| WINDOWCNT_FOR_STABILITY | slider | i | 2 to 4097 | 9 | 2 to 64 | `val`  Attempt to retain only peaks which are STABLE over time. | `-sVALUE` when SEND is ticked |

### focus fold: OCTAVE-TRANSPOSE SPECTRAL COMPONENTS INTO SPECIFIED RANGE

**No modes**  (`focus fold ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | D | 5 to 22050 | 5 |  | `lofrq` | as set (a file name when BRK) |
| HIGH_FRQ_LIMIT | slider (pitch-log) | D | 5 to 22050 | 22050 |  | `hifrq` | as set (a file name when BRK) |
| MORE_BODY_IN_SPECTRUM | check button |  |  |  |  | `-x` | `-x` when ticked |

### focus freeze: FREEZE SPECTRAL CHARACTERISTICS IN SOUND, AT GIVEN TIMES

**Mode 1**: freeze channel amplitudes  (`focus freeze 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FREEZE_DATA | data file |  |  |  |  | `datafile` | as set |

**Mode 2**: freeze channel frequencies  (`focus freeze 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FREEZE_DATA | data file |  |  |  |  | `datafile` | as set |

**Mode 3**: freeze channel amplitudes & frequencies  (`focus freeze 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FREEZE_DATA | data file |  |  |  |  | `datafile` | as set |

### focus hold: HOLD SOUND SPECTRUM, AT GIVEN TIMES

**No modes**  (`focus hold ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| HOLD_DATA | data file |  |  |  |  | `datafile` | as set |

### focus step: STEP-FRAME THROUGH SOUND BY FREEZING SPECTRUM AT REGULAR TIME INTERVALS

**No modes**  (`focus step ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESTEP | slider | d | 0.015465 to 9.998027 | 0.25 | 0.015465 to 1 | `timestep`  duration of steps. Must be >= duration of 2 analysis frames. | as set |

## blur  (BLURRING OPERATIONS ON A SPECTRAL FILE)

### blur avrg: AVERAGE SPECTRAL ENERGY OVER N ADJACENT CHANNELS

**No modes**  (`blur avrg ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AVERAGING_SPAN | slider | I | 2 to 513 | 13 |  | `N` | as set (a file name when BRK) |

### blur blur: TIME-AVERAGE THE SPECTRUM

**No modes**  (`blur blur ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| BLURRING_FACTOR | slider | I | 1 to 1293 | 16 | 1 to 64 | `blurring`  is number of windows over which to average the spectrum. | as set (a file name when BRK) |

### blur suppress: SUPPRESS THE N LOUDEST PARTIALS (ON A WINDOW-BY-WINDOW BASIS)

**No modes**  (`blur suppress ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| CHANNELS_TO_SUPRESS | slider | I | 1 to 513 | 16 |  | `N`  is number of spectral components to reject. | as set (a file name when BRK) |

### blur chorus: CHORUSING BY RANDOMISING AMPLITUDES AND/OR FREQUENCIES OF PARTIALS

**Mode 1**: Randomise partial amplitudes.  (`blur chorus 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_SPREAD | slider (log) | D | 1 to 1028 | 2 |  | `aspread`  is maximum random scatter of partial-amps (Range 1-1028) | as set (a file name when BRK) |

**Mode 2**: Randomise partial frequencies.  (`blur chorus 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

**Mode 3**: Randomise partial frequencies upwards only.  (`blur chorus 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

**Mode 4**: Randomise partial frequencies downwards only.  (`blur chorus 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

**Mode 5**: Randomise partial amplitudes AND frequencies.  (`blur chorus 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_SPREAD | slider (log) | D | 1 to 1028 | 2 |  | `aspread`  is maximum random scatter of partial-amps (Range 1-1028) | as set (a file name when BRK) |
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

**Mode 6**: Randomise partial amplitudes, and frequencies upwards only.  (`blur chorus 6 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_SPREAD | slider (log) | D | 1 to 1028 | 2 |  | `aspread`  is maximum random scatter of partial-amps (Range 1-1028) | as set (a file name when BRK) |
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

**Mode 7**: Randomise partial amplitudes, and frequencies downwards only.  (`blur chorus 7 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_SPREAD | slider (log) | D | 1 to 1028 | 2 |  | `aspread`  is maximum random scatter of partial-amps (Range 1-1028) | as set (a file name when BRK) |
| FRQ_SPREAD | slider | D | 1 to 4 | 1.1 |  | `fspread`  is maximum random scatter of partial-frqs (Range 1-4) | as set (a file name when BRK) |

### blur drunk: MODIFY SOUND BY DRUNKEN WALK ALONG ANALYSIS WINDOWS.

**No modes**  (`blur drunk ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| RANGE_IN_WINDOWS | slider | i | 1 to 646 | 32 | 1 to 64 | `range`  is maximum step (in windows) for drunken walk. | as set |
| STARTTIME_IN_FILE | number box | d | 0 to 9.998027 | 0 |  | `starttime` | as set |
| OUTFILE_DURATION | slider | d | 0.007732 to 32767 | 2 | 0 to 40 | `duration`  is required duration of outfile after resynthesis. | as set |
| REJECT_ZERO_STEPS | check button |  |  |  |  | `-z` | `-z` when ticked |

### blur shuffle: SHUFFLE ORDER OF ANALYSIS WINDOWS IN FILE

**No modes**  (`blur shuffle ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| SHUFFLE_DATA | text (shuffle string) |  |  |  |  | `domain-image` | as set |
| GROUPSIZE_(WINDOWS) | slider | i | 1 to 32767 | 1 | 1 to 16 | `grpsize` | as set |

### blur weave: MODIFY SOUND BY WEAVING AMONGST ANALYSIS WINDOWS.

**No modes**  (`blur weave ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| WEAVE_DATA | data file |  |  |  |  | `weavfile` | as set |

### blur noise: PUT NOISE IN THE SPECTRUM

**No modes**  (`blur noise ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| NOISE_SATURATION | slider | D | 0 to 1 | 0 |  | `noise`  Range 0 (no noise in spectrum) to 1 (spectrum saturated with noise). | as set (a file name when BRK) |

### blur scatter: RANDOMLY THIN OUT THE SPECTRUM

**No modes**  (`blur scatter ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FRQ-BLOCKS_TO_KEEP | slider | I | 1 to 513 | 128 |  | `keep`  number of (randomly-chosen) blocks to keep, in each spectral window. | as set (a file name when BRK) |
| FRQ_RANGE_OF_BLOCK | slider | D | 43.066406 to 11025 | 43.066406 | 43.066406 to 200 | `blocksize` | `-bVALUE` when SEND is ticked |
| KEEP_RANDOM_SET_ONLY | check button |  |  |  |  | `-r` | `-r` when ticked |
| DON'T_NORMALISE | check button |  |  |  |  | `-n` | `-n` when ticked |

### blur spread: SPREAD PEAKS OF SPECTRUM, INTRODUCING CONTROLLED NOISINESS

**No modes**  (`blur spread ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |
| DEGREE_OF_SPREADING | slider | D | 0 to 1 | 1 |  | `spread`  degree of spreading of spectrum (Range 0-1 : Default 1). | `-sVALUE` when SEND is ticked |

## modify  (SOUND MODIFICATIONS (TIME DOMAIN))

### modify loudness: ADJUST LOUDNESS OF A SOUNDFILE

**Mode 1**: GAIN:  (`modify loudness 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| GAIN | slider | D | 0 to 32767 | 1 | 0 to 20 | `gain` | as set (a file name when BRK) |

**Mode 2**: dBGAIN:  (`modify loudness 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| GAIN | slider | D | -96 to 96 | 0 |  | `gain` | as set (a file name when BRK) |

**Mode 3**: NORMALISE:  (`modify loudness 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LEVEL | number box | d | 0.000031 to 1 | 0.9 |  | `level` | `-lVALUE` when SEND is ticked |

**Mode 4**: FORCE LEVEL:  (`modify loudness 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LEVEL | number box | d | 0.000031 to 1 | 0.9 |  | `level` | `-lVALUE` when SEND is ticked |

**Mode 5**: BALANCE:  (`modify loudness 5 ...`)

No settings of its own: RUN runs it as it is.

**Mode 6**: INVERT PHASE: Invert phase of the sound.  (`modify loudness 6 ...`)

No settings of its own: RUN runs it as it is.

**Mode 7**: FIND LOUDEST: find loudest file.  (`modify loudness 7 ...`)

No settings of its own: RUN runs it as it is.

**Mode 8**: EQUALISE:  (`modify loudness 8 ...`)

No settings of its own: RUN runs it as it is.

**Mode 11**: PROPORTIONAL:  (`modify loudness 11 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOUDNESS_ENVELOPE | data file |  |  |  |  | `gain` | as set |

**Mode 12**: PROPORTIONAL dB: adjust level with dB envelope stretched to dur of sound.  (`modify loudness 12 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOUDNESS_ENVELOPE | data file |  |  |  |  | `gain` | as set |

### modify space: CREATE OR ALTER DISTRIBUTION OF SOUND IN STEREO SPACE

**Mode 1**: PAN:  (`modify space 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| PAN_POSITION | slider | D | -256 to 256 | 0 | -1 to 1 | `pan` | as set (a file name when BRK) |
| PRESCALING | number box | d | 0.000031 to 2 | 0.7 |  | `prescale` | `-pVALUE` when SEND is ticked |

**Mode 2**: MIRROR:  (`modify space 2 ...`)

No settings of its own: RUN runs it as it is.

**Mode 3**: MIRRORPAN:  (`modify space 3 ...`)

No settings of its own: RUN runs it as it is.

**Mode 4**: NARROW:  (`modify space 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| NARROWING | number box | d | -1 to 1 | 1 |  | `narrowing` | as set |

### modify speed: CHANGE THE SPEED & PITCH OF THE SRC SOUND.

**Mode 1**: Vary speed/pitch of a sound.  (`modify speed 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TRANSPOSITION | slider (log) | D | 0.00383 to 256 | 1 | 0.25 to 4 | `speed` | as set (a file name when BRK) |
| OUTFILE_TIMES | check button |  |  |  |  | `-o` | `-o` when ticked |

**Mode 2**: Vary speed/pitch by constant (fractional) no. of semitones.  (`modify speed 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TRANSPOSITION | slider | D | -96 to 96 | 0 | -12 to 12 | `semitone-transpos` | as set (a file name when BRK) |
| OUTFILE_TIMES | check button |  |  |  |  | `-o` | `-o` when ticked |

**Mode 3**: Get information on varying speed in a time-changing manner.  (`modify speed 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TRANSPOSITION | slider (log) | D | 0.00383 to 256 | 1 | 0.25 to 4 | `speed` | as set (a file name when BRK) |
| OUTFILE_TIMES | check button |  |  |  |  | `-o` | `-o` when ticked |

**Mode 4**: Get info on time-variable speedchange in semitones.  (`modify speed 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TRANSPOSITION | slider | D | -96 to 96 | 0 | -12 to 12 | `semitone-transpos` | as set (a file name when BRK) |
| OUTFILE_TIMES | check button |  |  |  |  | `-o` | `-o` when ticked |

**Mode 5**: Accelerate or decelerate a sound.  (`modify speed 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| ACCELERATION | slider (log) | d | 0.0001 to 1000 | 1 | 0.5 to 2 | `accel` | as set |
| GOAL_TIME | number box | d | 0.01 to 10 | 1 |  | `goaltime` | as set |
| START_TIME | number box | d | 0 to 9.99 | 0 |  | `starttime` | `-sVALUE` when SEND is ticked |

**Mode 6**: Add vibrato to a sound.  (`modify speed 6 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| CYCLES_PER_SECOND | slider | D | 0 to 120 | 12 | 0 to 50 | `vibrate` | as set (a file name when BRK) |
| SEMITONE_DEPTH | slider | D | 0 to 96 | 0.667 | 0 to 12 | `vibdepth` | as set (a file name when BRK) |

### modify revecho: CREATE REVERB, ECHO, OR RESONANCE AROUND SOUND

**Mode 1**  (`modify revecho 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DELAY_(MS) | slider (log) | d | 0.022676 to 10000 | 20 | 0.022676 to 1000 | `delay`  Delay time, in milliseconds. | as set |
| DELAYED_SIGNAL_IN_MIX | slider | D | 0 to 1 | 0.5 |  | `mix`  amount of delayed signal in final mix: 0 gives 'dry' result. | as set (a file name when BRK) |
| FEEDBACK | slider | D | -1 to 1 | 0 |  | `feedback`  produces resonance related to delay time (with short times). | as set (a file name when BRK) |
| LENGTH_OF_TAIL_TO_SOUND | slider | d | 0 to 30 | 0 | 0 to 2 | `tail`  is time to allow delayed signal to decay to zero. | as set |
| INPUT_PRESCALING | slider (log) | d | 0.000031 to 1 | 1 |  | `prescale`  prescales input level, to avoid overload. | `-pVALUE` when SEND is ticked |
| INVERT_DELAYED_SIGNAL | check button |  |  |  |  | `-i` | `-i` when ticked |

**Mode 2**: VARYING DELAY  (`modify revecho 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DELAY_(MS) | slider (log) | d | 0.022676 to 10000 | 20 | 0.022676 to 1000 | `delay`  Delay time, in milliseconds. | as set |
| DELAYED_SIGNAL_IN_MIX | slider | d | 0 to 1 | 0.5 |  | `mix`  amount of delayed signal in final mix: 0 gives 'dry' result. | as set |
| FEEDBACK | slider | d | -1 to 1 | 0 |  | `feedback`  produces resonance related to delay time (with short times). | as set |
| LOW_FRQ_MODULATION_DEPTH | slider | d | 0 to 1 | 0 |  | `lfomod`  is the depth of delay-variation sweep. | as set |
| MODULATION_FRQ:_-ve_FOR_RANDOM | slider | d | -50 to 50 | 0 |  | `lfofreq`  is the freq of the sweep: -ve vals give random oscillations. | as set |
| PHASE_OF_MODULATION | slider | d | 0 to 1 | 0 |  | `lfophase`  is the start phase of the sweep. | as set |
| ENTRY_DELAY_OF_MODULATION | slider | d | 0 to 10 | 0 | 0 to 0.5 | `lfodelay`  is the time before the seep begins. | as set |
| LENGTH_OF_TAIL_TO_SOUND | slider | d | 0 to 30 | 0 | 0 to 2 | `tail`  is time to allow delayed signal to decay to zero. | as set |
| INPUT_PRESCALING | slider (log) | d | 0.000031 to 1 | 1 |  | `prescale`  prescales input level, to avoid overload. | `-pVALUE` when SEND is ticked |
| RANDOM_SEED | slider | i | 0 to 256 | 0 |  | `seed`  Nonzero value gives reproducible output (with same seed) | `-sVALUE` when SEND is ticked |

**Mode 3**: STADIUM ECHO  (`modify revecho 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| INPUT_GAIN | slider | d | 0.000031 to 1 | 0.645654 |  | `gain`  to apply to input signal: Default is 0.645654 | `-gVALUE` when SEND is ticked |
| LEVEL_LOSS_WITH_DISTANCE | slider | d | 0.000031 to 1 | 1 |  | `roll_off`  rate of loss of level across stadium (default 1) | `-rVALUE` when SEND is ticked |
| STADIUM_SIZE_MULTIPLIER | slider (log) | d | 0.000005 to 100 | 1 | 0.1 to 10 | `size`  multiplies average time between echoes: (Default time 0.1 secs). | `-sVALUE` when SEND is ticked |
| NUMBER_OF_ECHOS | slider | i | 2 to 1000 | 23 | 2 to 23 | `count`  number of stadium echoes: Default (max) is 1000 | `-eVALUE` when SEND is ticked |
| NORMALISE_OUTPUT | check button |  |  |  |  | `-n` | `-n` when ticked |

### modify brassage: GRANULAR RECONSTITUTION OF SOUNDFILE

**Mode 1**: PITCHSHIFT  (`modify brassage 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| PITCHSHIFT_(SEMIT) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `pitchshift`  is pitchshift in +|- (fractions of) semitones. | as set (a file name when BRK) |

**Mode 2**: TIMESTRETCH  (`modify brassage 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESHRINK | slider | D | 0 to 32767 | 1 | 0 to 8 | `velocity`  speed of advance in infile, relative to outfile. (>=0) This is inver... | as set (a file name when BRK) |

**Mode 3**: REVERB  (`modify brassage 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DENSITY | slider (log) | D | 2 to 16384 | 8 | 0.125 to 8 | `density`  amount of grain overlap (>0 : <1 leaves intergrain silence) Extremel... | as set (a file name when BRK) |
| PITCHSHIFT_(SEMIT) | slider | D | -0.33 to 0.33 | 0 |  | `pitch`  is pitchshift in +|- (fractions of) semitones. | as set (a file name when BRK) |
| GRAIN_LOUDNESS_RANGE | slider | D | 0 to 1 | 1 |  | `amp`  is gain on grains (range 0-1) (Default 1.0) use only if you want amp... | as set (a file name when BRK) |
| SEARCHRANGE_(MS) | slider | D | 0 to 50 | 0 |  | `range`  of search for nextgrain, before infile 'now' (Default 0 MS). | `-rVALUE` when SEND is ticked |

**Mode 4**: SCRAMBLE  (`modify brassage 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| GRAINSIZE_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `grainsize`  grainsize in MS (must be > 2 * splicelen) (Default 50) | as set (a file name when BRK) |
| SEARCHRANGE_(MS) | slider | D | 0 to 20000 | 0 | 0 to 200 | `range`  of search for nextgrain, before infile 'now' (Default 0 MS). | `-rVALUE` when SEND is ticked |

**Mode 5**: GRANULATE  (`modify brassage 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DENSITY | slider (log) | D | 0.000002 to 2 | 2 | 0.125 to 2 | `density`  amount of grain overlap (>0 : <1 leaves intergrain silence) Extremel... | as set (a file name when BRK) |
| NO_DECCELERATION | check button |  |  |  |  | `-d` | `-d` when ticked (in CDP's table, not its usage text) |

**Mode 6**: BRASSAGE  (`modify brassage 6 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESHRINK | slider | D | 0 to 32767 | 1 | 0 to 8 | `velocity`  speed of advance in infile, relative to outfile. (>=0) This is inver... | as set (a file name when BRK) |
| DENSITY | slider (log) | D | 0.000002 to 16384 | 2 | 0.125 to 8 | `density`  amount of grain overlap (>0 : <1 leaves intergrain silence) Extremel... | as set (a file name when BRK) |
| GRAINSIZE_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `grainsize`  grainsize in MS (must be > 2 * splicelen) (Default 50) | as set (a file name when BRK) |
| PITCHSHIFT_(SEMIT) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `pitchshft` | as set (a file name when BRK) |
| GRAIN_LOUDNESS_RANGE | slider | D | 0 to 1 | 1 |  | `amp`  is gain on grains (range 0-1) (Default 1.0) use only if you want amp... | as set (a file name when BRK) |
| SPATIAL_POSITION | slider | D | 0 to 16 | 0.5 |  | `space`  set stereo position in outputfile. 0=L,1=R (Range 0-1). Space flag o... | as set (a file name when BRK) |
| STARTSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `bsplice`  length of startsplices on grains,in MS (Default 5) | as set (a file name when BRK) |
| ENDSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `esplice`  length of endsplices on grains,in MS (Default 5) | as set (a file name when BRK) |
| SEARCHRANGE_(MS) | slider | D | 0 to 20000 | 0 | 0 to 200 | `range`  of search for nextgrain, before infile 'now' (Default 0 MS). | `-rVALUE` when SEND is ticked |
| SCATTER | slider | D | 0 to 1 | 0.5 |  | `jitter`  Randomisation of grain position (Range 0-1) Default (0.50). | `-jVALUE` when SEND is ticked |
| OUTPUT_LENGTH_(SECS) | slider | d | 0 to 32767 | 0 | 0 to 60 | `outlength`  maximum outfile length (if end of data not reached). Set to zero (De... | `-lVALUE` when SEND is ticked |
| INPUT_CHAN_TO_EXTRACT_(-ve_N:_OUTPUT_SPATIALISE_TO_N_CHANS) | slider | i | -16 to 16 | 0 |  | `channel`  Extract & work on just 1 channel of stereo snd: Range(1-2). | `-cVALUE` when SEND is ticked |
| NO_DECCELERATION | check button |  |  |  |  | `-d` | `-d` when ticked (in CDP's table, not its usage text) |
| EXPONENTIAL_SPLICES | check button |  |  |  |  | `-x` | `-x` when ticked |
| PITCH_NOT_INTERPOLATED | check button |  |  |  |  | `-n` | `-n` when ticked |

**Mode 7**: FULL MONTY  (`modify brassage 7 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESHRINK | slider | D | 0 to 32767 | 1 | 0 to 8 | `velocity`  speed of advance in infile, relative to outfile. (>=0) This is inver... | as set (a file name when BRK) |
| DENSITY | slider (log) | D | 0.000002 to 16384 | 2 | 0.125 to 8 | `density`  amount of grain overlap (>0 : <1 leaves intergrain silence) Extremel... | as set (a file name when BRK) |
| TIMESHRINK_LIMIT | slider | D | 0 to 32767 | 1 | 0 to 8 | `hvelocity` | as set (a file name when BRK) |
| DENSITY_LIMIT | slider (log) | D | 0.000023 to 16383.5 | 2 | 0.125 to 8 | `hdensity` | as set (a file name when BRK) |
| GRAINSIZE_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `grainsize`  grainsize in MS (must be > 2 * splicelen) (Default 50) | as set (a file name when BRK) |
| PITCHSHIFT_(SEMIT) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `pitchshift`  is pitchshift in +|- (fractions of) semitones. | as set (a file name when BRK) |
| GRAIN_LOUDNESS_RANGE | slider | D | 0 to 1 | 1 |  | `amp`  is gain on grains (range 0-1) (Default 1.0) use only if you want amp... | as set (a file name when BRK) |
| SPATIAL_POSITION | slider | D | 0 to 16 | 0.5 |  | `space`  set stereo position in outputfile. 0=L,1=R (Range 0-1). Space flag o... | as set (a file name when BRK) |
| STARTSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `bsplice`  length of startsplices on grains,in MS (Default 5) | as set (a file name when BRK) |
| ENDSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `esplice`  length of endsplices on grains,in MS (Default 5) | as set (a file name when BRK) |
| GRAINSIZE_LIMIT_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `hgrainsize` | as set (a file name when BRK) |
| PITCHSHIFT_LIMIT_(SEMIT) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `hpitchshift` | as set (a file name when BRK) |
| LOUDNESS_RANGE_LIMIT | slider | D | 0 to 1 | 1 |  | `hamp` | as set (a file name when BRK) |
| SPATIAL_POSITION_LIMIT | slider | D | 0 to 16 | 0.5 |  | `hspace` | as set (a file name when BRK) |
| STARTSPLICE_LIMIT_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `hbsplice` | as set (a file name when BRK) |
| ENDSPLICE_LIMIT_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `hesplice` | as set (a file name when BRK) |
| SEARCHRANGE_(MS) | slider | D | 0 to 20000 | 0 | 0 to 200 | `range`  of search for nextgrain, before infile 'now' (Default 0 MS). | `-rVALUE` when SEND is ticked |
| SCATTER | slider | D | 0 to 1 | 0.5 |  | `jitter`  Randomisation of grain position (Range 0-1) Default (0.50). | `-jVALUE` when SEND is ticked |
| OUTPUT_LENGTH_(SECS) | slider | d | 0 to 32767 | 0 | 0 to 60 | `outlength`  maximum outfile length (if end of data not reached). Set to zero (De... | `-lVALUE` when SEND is ticked |
| INPUT_CHAN_TO_EXTRACT_(-ve_N:_OUTPUT_SPATIALISE_TO_N_CHANS) | slider | i | -16 to 16 | 0 |  | `channel`  Extract & work on just 1 channel of stereo snd: Range(1-2). | `-cVALUE` when SEND is ticked |
| NO_DECCELERATION | check button |  |  |  |  | `-d` | `-d` when ticked (in CDP's table, not its usage text) |
| EXPONENTIAL_SPLICES | check button |  |  |  |  | `-x` | `-x` when ticked |
| PITCH_NOT_INTERPOLATED | check button |  |  |  |  | `-n` | `-n` when ticked |

### modify sausage: GRANULAR RECONSTITUTION OF SEVERAL SOUNDFILES SCRAMBLED TOGETHER.

**No modes**  (`modify sausage ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TIMESHRINK | slider | D | 0 to 32767 | 1 | 0 to 8 | `velocity`  speed of advance in infiles, relative to outfile. (>=0) inverse of t... | as set (a file name when BRK) |
| DENSITY | slider (log) | D | 0.000002 to 16384 | 2 | 0.125 to 8 | `density`  grain overlap (>0 : <1 leaves intergrain silence) Extremely small va... | as set (a file name when BRK) |
| TIMESHRINK_LIMIT | slider | D | 0 to 32767 | 1 | 0 to 8 | `hvelocity` | as set (a file name when BRK) |
| DENSITY_LIMIT | slider (log) | D | 0.000023 to 16383.5 | 2 | 0.125 to 8 | `hdensity` | as set (a file name when BRK) |
| GRAINSIZE_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `grainsize`  grainsize in MS (must be > 2 * splicelen) (Default 50) | as set (a file name when BRK) |
| PITCHSHIFT(SEMITONES) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `pitchshift`  pitchshift in +|- (fractions of) semitones. | as set (a file name when BRK) |
| GRAIN_LOUDNESS_RANGE | slider | D | 0 to 1 | 1 |  | `amp`  gain on grains (range 0-1) (Default 1.0) use if amp variation requir... | as set (a file name when BRK) |
| SPATIAL_POSITION | slider | D | 0 to 16 | 0.5 |  | `space`  stereo position in outputfile. 0=L,1=R (Range 0-1). Space flag on ST... | as set (a file name when BRK) |
| STARTSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `bsplice`  grain-startsplice length,in MS (Default 5) | as set (a file name when BRK) |
| ENDSPLICE_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `esplice`  grain-endsplice length,in MS (Default 5) | as set (a file name when BRK) |
| GRAINSIZE_LIMIT_(MS) | slider | D | 2 to 10000 | 50 | 2 to 200 | `hgrainsize` | as set (a file name when BRK) |
| PITCHSHIFT_LIMIT_(SEMIT) | slider | D | -133.278752 to 133.278752 | 0 | -12 to 12 | `hpitchshift` | as set (a file name when BRK) |
| LOUDNESS_RANGE_LIMIT | slider | D | 0 to 1 | 1 |  | `hamp` | as set (a file name when BRK) |
| SPATIAL_POSITION_LIMIT | slider | D | 0 to 16 | 0.5 |  | `hspace` | as set (a file name when BRK) |
| STARTSPLICE_LIMIT_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `hbsplice` | as set (a file name when BRK) |
| ENDSPLICE_LIMIT_(MS) | slider | D | 1 to 5000 | 5 | 1 to 20 | `hesplice` | as set (a file name when BRK) |
| SEARCHRANGE_(MS) | slider | D | 0 to 20000 | 0 | 0 to 200 | `range`  of search for nextgrain, before infile 'now' (Default 0 MS). | `-rVALUE` when SEND is ticked |
| SCATTER | slider | D | 0 to 1 | 0.5 |  | `jitter`  Randomisation of grain position (Range 0-1) Default (0.50). | `-jVALUE` when SEND is ticked |
| OUTPUT_LENGTH_(SECS) | slider | d | 0 to 32767 | 0 | 0 to 60 | `outlength`  max outfile length (if end of data not reached). Set to zero (Defaul... | `-lVALUE` when SEND is ticked |
| INPUT_CHAN_TO_EXTRACT_(-ve_N:_OUTPUT_SPATIALISE_TO_N_CHANS) | slider | i | -16 to 16 | 0 |  | `channel`  work on just 1 channel of stereo snd: Range(1-2). Set to zero (Defau... | `-cVALUE` when SEND is ticked |
| NO_DECCELERATION | check button |  |  |  |  | `-d`  grain overlap (>0 : <1 leaves intergrain silence) Extremely small va... | `-d` when ticked |
| EXPONENTIAL_SPLICES | check button |  |  |  |  | `-x` | `-x` when ticked |
| PITCH_NOT_INTERPOLATED | check button |  |  |  |  | `-n` | `-n` when ticked |

### modify radical: RADICAL CHANGES TO THE SOUND.

**Mode 1**: REVERSE:  (`modify radical 1 ...`)

No settings of its own: RUN runs it as it is.

**Mode 2**: SHRED:  (`modify radical 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| NUMBER_OF_SHREDS | slider | i | 1 to 10000 | 1 | 1 to 2000 | `repeats` | as set |
| AVERAGE_CHUNKLENGTH | slider | d | 0.017415 to 4.999998 | 1.25 |  | `chunklen` | as set |
| CUT_SCATTER | slider | d | 0 to 8 | 1 | 0 to 1 | `scatter` | `-sVALUE` when SEND is ticked |
| SMOOTHER | check button |  |  |  |  | `-n` | `-n` when ticked |

**Mode 3**: SCRUB BACK & FORTH: as if handwinding over a tape-head.  (`modify radical 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| MINIMUM_OUTPUT_DURATION | slider | d | 0.000023 to 32767 | 10 | 0.000023 to 20 | `dur` | as set |
| LOWEST_DOWNWARD_TRANSPOSITION | slider | d | -96 to 96 | -48 | -48 to 0 | `up` | `-hVALUE` when SEND is ticked |
| HIGHEST_UPWARD_TRANSPOSITION | slider | d | -96 to 96 | 48 | 0 to 48 | `down` | `-lVALUE` when SEND is ticked |
| SCRUBS_START_NO_LATER_THAN | slider | d | 0 to 9.999977 | 0 |  | `start` | `-sVALUE` when SEND is ticked |
| SCRUBS_END_NO_EARLIER_THAN | slider | d | 0.000023 to 10 | 10 |  | `end` | `-eVALUE` when SEND is ticked |
| SINGLE_SCRUB_(version_7+_only) | check button |  |  |  |  | `-f` | `-f` when ticked |

**Mode 4**: LOSE RESOLUTION: sound converted to lower srate, or bit-resolution.  (`modify radical 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| BIT_RESOLUTION | slider | i | 1 to 16 | 16 |  | `bit_resolution` | as set |
| SAMPLING_RATE_DIVISION | slider | i | 1 to 256 | 1 |  | `srate_division` | as set |

**Mode 5**: RING MODULATE:  (`modify radical 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| MODULATING_FREQUENCY | slider (pitch-log) | D | 0.1 to 22050 | 60 | 16 to 400 | `modulating-frq` | as set (a file name when BRK) |

**Mode 6**: CROSS MODULATE:  (`modify radical 6 ...`)

No settings of its own: RUN runs it as it is.

**Mode 7**: QUANTISE:  (`modify radical 7 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| BIT_RESOLUTION | slider | i | 1 to 16 | 16 |  | `bit_resolution` | as set |

### modify spaceform: CREATE A SINUSOIDAL SPATIAL DISTRIBUTION DATA FILE

**No modes**  (`modify spaceform ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| CYCLE_LENGTH | slider (log) | D | 0.01 to 100 | 1 |  | `cyclelen`  is the duration of one complete sinusoidal pan cycle | as set (a file name when BRK) |
| PAN_WIDTH | slider | D | 0 to 1 | 1 |  | `width`  is the width of the pan (from 0 to full width,1) | as set (a file name when BRK) |
| DURATION | slider | d | 0 to 32767 | 10 | 1 to 60 | `dur`  is the duration of the output file. | as set |
| QUANTISATION | slider (log) | d | 0.005 to 1 | 0.01 |  | `quantisation` | as set |
| START_PHASE | slider | d | 0 to 360 | 0 |  | `phase`  is the angular position at which the pan starts. | as set |

### modify scaledpan: DISTRIBUTE SOUND IN STEREO SPACE, SCALING PAN DATA TO SNDFILE DURATION

**No modes**  (`modify scaledpan ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| PAN_POSITION | slider | D | -256 to 256 | 0 | -1 to 1 | `pan`  breakpoint file of time, position pairs. | as set (a file name when BRK) |
| PRESCALING | number box | d | 0.000031 to 2 | 0.7 |  | `prescale` | `-pVALUE` when SEND is ticked |

### modify findpan: FIND STEREO-PAN POSITION OF A SOUND IN A STEREO FILE

**No modes**  (`modify findpan ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AT_TIME | number box | d | 0 to 10 | 0 |  | `time` | as set |

### modify convolve: CONVOLVE THE FIRST SOUND WITH THE SECOND

**Mode 1**  (`modify convolve 1 ...`)

No settings of its own: RUN runs it as it is.

**Mode 2**  (`modify convolve 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| SEMITONE_TRANSPOSITION_OF_CONVOLVING_FILE | slider | D | -48 to 48 | 0 | -12 to 12 | `tranposfile` | as set (a file name when BRK) |

### modify stack: CREATE A MIX WHICH STACKS TRANPOSED VERSIONS OF SOURCE ON ONE ANOTHER

**No modes**  (`modify stack ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| TRANSPOSITION_DATA | number or data file | D | -60 to 60 | 12 |  | `transpos`  when numeric, is (semitone) transposition between successive copies | as set (a file name when BRK) |
| NUMBER_OF_ITEMS | number box | i | 2 to 32 | 3 |  | `count`  is the number of copies in the stack. | as set |
| STACK_LEAN | number box (log) | d | 0.01 to 100 | 1 |  | `lean`  is the loudness of the highest component, relative to the lowest. | as set |
| ATTACK_TIME | number box | d | 0 to 10 | 0 |  | `atk-offset` | as set |
| OVERALL_GAIN | number box (log) | d | 0.1 to 10 | 1 |  | `gain`  is an overall amplifying factor on the ouptut sound. | as set |
| HOW_MUCH_OF_OUPUT_TO_MAKE | number box | d | 0 to 1 | 1 |  | `dur`  how much of the output to make (a proportion, from 0 to 1). | as set |
| SEE_STACK_LEVELS | check button |  |  |  |  | `-s` | `-s` when ticked |
| NORMALISE | check button |  |  |  |  | `-n` | `-n` when ticked |

### modify shudder: SHUDDER A STEREO FILE

**No modes**  (`modify shudder ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| START_TIME | number box | d | 0 to 10 | 0 |  | `starttime` | as set |
| FREQUENCY | slider (log) | D | 0.1 to 100 | 6 |  | `frq` | as set (a file name when BRK) |
| RANDOMISATION | slider | D | 0 to 1 | 1 |  | `scatter`  randomises the shudder events, in time. (0 - 1) | as set (a file name when BRK) |
| SPATIAL_SPREAD | slider | D | 0 to 1 | 1 |  | `stereo_spread` | as set (a file name when BRK) |
| LOUDNESS_DEPTH_MINIMUM | slider | D | 0 to 1 | 0 |  | `mindepth` | as set (a file name when BRK) |
| LOUDNESS_DEPTH_MAXIMUM | slider | D | 0 to 1 | 1 |  | `maxdepth` | as set (a file name when BRK) |
| EVENT_WIDTH_MINIMUM | slider (log) | D | 0.02 to 10 | 0.02 |  | `minwidth` | as set (a file name when BRK) |
| EVENT_WIDTH_MAXIMUM | slider (log) | D | 0.02 to 10 | 0.2 |  | `maxwidth` | as set (a file name when BRK) |
| BALANCE_CHANNEL_LEVELS | check button |  |  |  |  | `-b` | `-b` when ticked |

## combine  (COMBINATION OF SPECTRAL FILES)

### combine make: GENERATE SPECTRUM FROM PITCH & FORMANT DATA ONLY.

**No modes**  (`combine make ...`)

Inputs CDP reads as files (no controls on the page): `pitchfile`, `formantfile`

### combine make2: GENERATE SPECTRUM FROM PITCH, FORMANT AND ENVELOPE DATA ONLY.

**No modes**  (`combine make2 ...`)

Inputs CDP reads as files (no controls on the page): `pitchfile`, `formantfile`, `envfile`

### combine sum: FIND SUM OF TWO SPECTRA.

**No modes**  (`combine sum ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| CROSSOVER | slider | D | 0 to 1 | 1 |  | `crossover`  CROSSOVER is amount of 2nd spectrum added to 1st (range 0 to 1) | `-cVALUE` when SEND is ticked |

### combine diff: FIND DIFFERENCE OF TWO SPECTRA.

**No modes**  (`combine diff ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| CROSSOVER | slider | D | 0 to 1 | 1 |  | `crossover`  CROSSOVER is amount of 2nd spectrum subtracted from 1st (range 0 to 1) | `-cVALUE` when SEND is ticked |
| RETAIN_SUBZERO_AMPLITUDES | check button |  |  |  |  | `-a` | `-a` when ticked |

### combine interleave: INTERLEAVE WINDOWS FROM INFILES, LEAFSIZE WINDOWS PER LEAF.

**No modes**  (`combine interleave ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LEAFSIZE_(WINDOWS) | slider | i | 1 to 1293 | 1 | 1 to 16 | `leafsize` | as set |

### combine max: IN EACH ANALYSIS CHANNEL, IN EACH WINDOW, TAKE MAX VAL AMONGST INPUT FILES.

**No modes**  (`combine max ...`)

No settings of its own: RUN runs it as it is.

### combine mean: GENERATE SPECTRAL 'MEAN' OF 2 SOUNDS.

**Mode 1**: mean channel amp of 2 files :  (`combine mean 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 2**: mean channel amp of 2 files :  (`combine mean 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 3**: channel amp from file1  (`combine mean 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 4**: channel amp from file1  (`combine mean 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 5**: channel amp from file2  (`combine mean 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 6**: channel amp from file2  (`combine mean 6 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 7**: max channel amp of 2 files  (`combine mean 7 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 8**: max channel amp of 2 files  (`combine mean 8 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| LOW_FRQ_LIMIT | slider (pitch-log) | d | 5 to 22028.466797 | 5 |  | `lofrq`  is low freq limit of channels to look at. | `-lVALUE` when SEND is ticked |
| HIGH_FRQ_LIMIT | slider (pitch-log) | d | 48.066406 to 22050 | 22050 |  | `hifrq`  is high freq limit of channels to look at. | `-hVALUE` when SEND is ticked |
| CHANNELS_TO_COMPARE | number box | i | 2 to 513 | 513 |  | `chans` | `-cVALUE` when SEND is ticked |
| ZERO_CHANNELS_OUTSIDE_RANGE | check button |  |  |  |  | `-z` | `-z` when ticked |

### combine cross: REPLACE SPECTRAL AMPLITUDES OF 1st FILE WITH THOSE OF 2nd.

**No modes**  (`combine cross ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| INTERPOLATION | slider | D | 0 to 1 | 1 |  | `interp` | `-iVALUE` when SEND is ticked |

## morph  (MORPHING BETWEEN SPECTRAL FILES)

### morph glide: INTERPOLATE, LINEARLY, BETWEEN 2 SINGLE ANALYSIS WINDOWS

**No modes**  (`morph glide ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| DURATION | number box | d | 0.015465 to 32767 | 2 |  | `duration`  is duration of output sound required. | as set |

### morph bridge: MAKE A BRIDGING-INTERPOLATION BETWEEN TWO SOUND SPECTRA

**Mode 1**: output level is direct result of interpolation.  (`morph bridge 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

**Mode 2**: output level follows moment to moment minimum of the 2 infile amplitudes.  (`morph bridge 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

**Mode 3**: output level follows moment to moment amplitude of infile1.  (`morph bridge 3 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

**Mode 4**: output level follows moment to moment amplitude of infile2.  (`morph bridge 4 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

**Mode 5**: output level moves, through interp, from that of file1 to that of file2.  (`morph bridge 5 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

**Mode 6**: output level moves, through interp, from that of file2 to that of file1.  (`morph bridge 6 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| 2ND_FILE_OFFSET | number box | d | 0 to 9.982562 | 0 |  | `offset` | `-aVALUE` when SEND is ticked |
| FRQ_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sf2`  fraction of 2nd sound's frq interpolated at START.(default 0) | `-bVALUE` when SEND is ticked |
| AMP_INTERP_AT_START | number box | d | 0 to 1 | 0 |  | `sa2`  fraction of 2nd sound's amp interpolated at START.(default 0) | `-cVALUE` when SEND is ticked |
| FRQ_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ef2`  fraction of 2nd sound's frq interpolated at END.(default 1) | `-dVALUE` when SEND is ticked |
| AMP_INTERP_AT_END | number box | d | 0 to 1 | 1 |  | `ea2`  fraction of 2nd sound's amp interpolated at END.(default 1) | `-eVALUE` when SEND is ticked |
| STARTTIME_IN_FILE1 | number box | d | 0 to 9.982562 | 0 |  | `start`  time in infile1, of startwindow for interp, (secs): default: 0.0 | `-fVALUE` when SEND is ticked |
| ENDTIME_IN_FILE1 | number box | d | 0.015465 to 9.998027 | 0.015465 |  | `end`  time in infile1 of endwindow of interp: default: end_of_file | `-gVALUE` when SEND is ticked |

### morph morph: MORPH ONE SPECTRUM INTO ANOTHER.

**Mode 1**: interpolate linearly (exp  (`morph morph 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_INTERP_START | number box | d | 0 to 9.990294 | 0 |  | `as`  is time(secs) when amplitude-interpolation starts. | as set |
| AMP_INTERP_END | number box | d | 0.015465 to 32767 | 0.015465 |  | `ae`  is time(secs) when amplitude-interpolation ends. | as set |
| FRQ_INTERP_START | number box | d | 0 to 9.990294 | 0 |  | `fs`  is time(secs) when frequency-interpolation starts. | as set |
| FRQ_INTERP_END | number box | d | 0.015465 to 32767 | 0.015465 |  | `fe`  is time(secs) when frequency-interpolation ends. | as set |
| AMP_INTERP_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `expa` | as set |
| FRQ_INTERP_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `expf` | as set |
| 2nd_FILE_ENTRY_TIME | number box | d | 0 to 9.990294 | 0 |  | `stagger` | `-sVALUE` when SEND is ticked |

**Mode 2**: interpolate over a cosinusoidal spline.  (`morph morph 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| AMP_INTERP_START | number box | d | 0 to 9.990294 | 0 |  | `as`  is time(secs) when amplitude-interpolation starts. | as set |
| AMP_INTERP_END | number box | d | 0.015465 to 32767 | 0.015465 |  | `ae`  is time(secs) when amplitude-interpolation ends. | as set |
| FRQ_INTERP_START | number box | d | 0 to 9.990294 | 0 |  | `fs`  is time(secs) when frequency-interpolation starts. | as set |
| FRQ_INTERP_END | number box | d | 0.015465 to 32767 | 0.015465 |  | `fe`  is time(secs) when frequency-interpolation ends. | as set |
| AMP_INTERP_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `expa` | as set |
| FRQ_INTERP_EXPONENT | slider (log) | d | 0.02 to 50 | 1 | 0.25 to 4 | `expf` | as set |
| 2nd_FILE_ENTRY_TIME | number box | d | 0 to 9.990294 | 0 |  | `stagger` | `-sVALUE` when SEND is ticked |

## repitch  (PITCH DATA FROM SPECTRAL FILES)

### repitch getpitch: ATTEMPT TO EXTRACT PITCH FROM SPECTRAL DATA.

**Mode 1**  (`repitch getpitch 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| IN-TUNE_RANGE_(SEMIT) | number box | d | 0 to 6 | 1 |  | `R` | `-tVALUE` when SEND is ticked |
| MIN_WINDOWS_TO_CONFIRM_PITCH | slider | i | 0 to 1293 | 2 | 0 to 8 | `M` | `-gVALUE` when SEND is ticked |
| SIGNAL_TO_NOISE_RATIO_(dB) | number box | d | 0 to 1000 | 80 |  | `S` | `-sVALUE` when SEND is ticked |
| VALID_HARMONICS_COUNT | number box | i | 1 to 8 | 5 |  | `H` | `-nVALUE` when SEND is ticked |
| LOW_PITCH_LIMIT_(HZ) | slider (pitch-log) | d | 10 to 2756.25 | 10 |  | `L` | `-lVALUE` when SEND is ticked |
| HIGH_PITCH_LIMIT_(HZ) | slider (pitch-log) | d | 10 to 2756.25 | 2756.25 |  | `T` | `-hVALUE` when SEND is ticked |
| USE_OTHER_ALGORITHM | check button |  |  |  |  | `-a` | `-a` when ticked |
| KEEP_PITCH_ZEROS | check button |  |  |  |  | `-z` | `-z` when ticked |

**Mode 2**  (`repitch getpitch 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| IN-TUNE_RANGE_(SEMIT) | number box | d | 0 to 6 | 1 |  | `R` | `-tVALUE` when SEND is ticked |
| MIN_WINDOWS_TO_CONFIRM_PITCH | slider | i | 0 to 1293 | 2 | 0 to 8 | `M` | `-gVALUE` when SEND is ticked |
| SIGNAL_TO_NOISE_RATIO_(dB) | number box | d | 0 to 1000 | 80 |  | `S` | `-sVALUE` when SEND is ticked |
| VALID_HARMONICS_COUNT | number box | i | 1 to 8 | 5 |  | `H` | `-nVALUE` when SEND is ticked |
| LOW_PITCH_LIMIT_(HZ) | slider (pitch-log) | d | 10 to 2756.25 | 10 |  | `L` | `-lVALUE` when SEND is ticked |
| HIGH_PITCH_LIMIT_(HZ) | slider (pitch-log) | d | 10 to 2756.25 | 2756.25 |  | `T` | `-hVALUE` when SEND is ticked |
| DATAREDUCTION_(SEMIT) | slider | d | 0.00002 to 12 | 0.25 | 0.00002 to 1 | `i` | `-dVALUE` when SEND is ticked |
| USE_OTHER_ALGORITHM | check button |  |  |  |  | `-a` | `-a` when ticked |

### repitch analenv: EXTRACT THE WINDOW-LOUDNESS ENVELOPE OF AN ANALYSIS FILE

**No modes**  (`repitch analenv ...`)

Inputs CDP reads as files (no controls on the page): `outenvfile`

## formants  (FORMANT DATA FROM SPECTRAL FILES)

### formants get: EXTRACT EVOLVING FORMANT ENVELOPE FROM AN ANALYSIS FILE.

**No modes**  (`formants get ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| FORMANT_BANDS | two-way choice + slider | i | PITCHWISE 1 to 12; FRQWISE 1 to 256 | 4 / 4 |  | `-fN|-pN` | `-pN` or `-fN` |
| FORMANT_QUICKSEARCH | check button |  |  |  |  | `-i` | `-i` when ticked |

## envel  (ENVELOPE OPERATIONS)

### envel extract: EXTRACT ENVELOPE FROM AN INPUT SOUNDFILE.

**Mode 1**  (`envel extract 1 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| WINDOW_SIZE_(MS) | slider | d | 5 to 10000 | 50 | 5 to 1000 | `wsize`  window_size (in MS) for scanning envelope: Range (5 - filelen) | as set |

**Mode 2**  (`envel extract 2 ...`)

| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |
|---|---|---|---|---|---|---|---|
| WINDOW_SIZE_(MS) | slider | d | 5 to 10000 | 50 | 5 to 1000 | `wsize`  window_size (in MS) for scanning envelope: Range (5 - filelen) | as set |
| DATA_REDUCTION | slider | d | 0 to 1 | 0.0002 |  | `datareduce` | `-dVALUE` when SEND is ticked |


# THE ROOMS (build 12)

The strip under the banner (**YOUR SOUND GOES: GROW > THE LAB > STRETCH > REBORN > SPACE > INTO FL**) opens a page for each
room. The order on the strip is the usual route, not a rule: every room works on the sound as it is now, and every run is one
step in the history (UNDO takes it back). Running the same room again replaces its last run, so nothing stacks up while you
tweak. Each page has the same three parts: what the room does and the choices (left), THE PICTURE of the sound before and
after (middle: time across, low notes at the bottom, brighter = louder), a few big controls and one big button (right).
A control that would do nothing right now is greyed.

| Room | Its engine (About / Credits) | Controls -> the engine's own controls |
|---|---|---|
| **GROW** | Flues, through its own LV2 plug-in code at its .ttl defaults | WHAT KIND / WHICH ONE = the plug-in and its interface model or vowel preset; LOUD, BRIGHT, ROUGH, SHAPE move the plug-in's own ports (Source/Matter/Engine/MakeStage); HOW LONG, NOTE. GROW IT = a new SPECIMEN 001; AS SECOND SOUND = SPECIMEN 002 |
| **STRETCH** | PaulXStretch, its own offline render chain | HOW FAR = Stretch amount (1..64); SMOOTH = FFT size; SMEAR = Frequency spread; OCTAVE ABOVE / BELOW = the Ratios module's octave levels; PITCH = Pitch shift. START FROM chips set these |
| **REBORN** | DDSP-VST, its own processor run offline | PLAYED BY = its model; HOW MUCH = dry / wet (ours); MOVE THE TUNE = its Pitch Shift; TONE = Harmonics; BREATH = Noise; CHAOS / SWAP / FEED BACK = ours on top (they only move its controls, switch its model, feed its output back). AUTO sets Pitch Shift by whole octaves so the tune sits in the instrument's playing range; the picture shows the tune (white line) against that range (the band) |
| **SPACE** | Pertylizer's Spatial Panner and Reverb | the pad = X and Distance; ROOM = early reflections; TAIL = reverb; WIDTH = how far apart the two sides sit. LISTEN plays the sound round and round through the engine live while you drag; PRINT IT renders it into the history |

Not on a page yet (compiled in, from Ghost Matter): BOUNCE (Box2D balls striking Flues voices), Pertylizer's Euclidean and
Turing Machine pattern generators, the full "every control" pages for these engines, and WORDS (the optional Bark helper).

## Build 13: the rooms laid out like the originals

GROW is off the strip (its engine is still compiled in). Each remaining room's page follows the original plug-in's own
window and uses its own control names, ranges and defaults (from the engines' control tables, `Source/Matter/Hood`), with
one line in plain words under each:

- **STRETCH** = PaulXStretch: RENDER and the info line on top; its parameter groups stacked in its order (Main, Harmonics,
  Tonal vs Noise, Frequency shift | Pitch shift, Frequency spread | Compress, Ratios, Filter, Binaural beats), each spectral
  module's group with its own on / off switch (the original's defaults: Frequency shift and Pitch shift on); its
  processing-chain strip; the sound display (before at true size, after). Stretch amount opens at 8x (the original: 2x) and
  stops at 64x. Not on the page yet: the Free filter curve, the ratio values, the chain re-ordering, the play range.
- **REBORN** = DDSP-VST: the model list on top; the Pitch / Volume box (the model's range, draggable = Input Pitch / Input
  Gain; the dots are the sound's own notes after Pitch Shift); its knobs along the bottom (Pitch Shift, Harmonics, Noise,
  Output Gain, Reverb Size / Damp / Wet). AUTO FIT (ours) sets Pitch Shift by whole octaves.
- **SPACE** = Pertylizer's rack: the Spatial Panner module (X, Y, Z, Diffusion, ER Level, Direct Level, Absorption, Air
  Absorption, Distance) wired into the Reverb module (Room Size, Decay, Damping, Diffusion, Pre-Delay, Low Cut, Width, Mix),
  every knob sent to the engine exactly as shown. The view from above (ours) turns X and Distance.

## Build 14: the rest of STRETCH

The three parts build 13 left off the STRETCH page are on it: the Ratios group has all eight levels and all eight ratio
values; the processing chain is re-ordered by dragging a module onto another place (the plug-in's own chain order); the
Free filter group has the plug-in's own curve editor (its `EnvelopeComponent`, unmodified but for the marked colour
shims: double-click adds a node, drag nodes and segments, right-click for its own menu) above its seven transform sliders.
The curve is kept with the settings (and the session) in the plug-in's own saved form. Still not on the page: the play range.

## Build 15: every Main control of the original

Sound start / Sound end (the play range), Bypass stretch, Dry playrate (live only with Bypass stretch on), Loop xfade
length, Pass input through and Max capture length are in the Main group. The last three are shown greyed: Ghost Lab renders
the sound once (no loop, no live input, no recording), so they have nothing to act on. Every control in the original's
parameter list is now on the STRETCH page; its chain order is the strip.

## Build 16: TRIM

Each waveform strip has a handle at each end. Drag one in (the part that will go dims, and the strip shows KEEP a TO b);
let go and it is trimmed, with a 5 ms fade where it was cut (an edge you did not move is left alone). Shortest keep: 0.05 s.
- **ORIGINAL**: the part kept becomes the new original (SPECIMEN 001) that every effect, room and GENERATE MUTATION works
  on; PROCESSED starts again from it. One history step: UNDO brings back the whole original and the result you had (each
  history step carries the original in force at that step).
- **PROCESSED**: the result only, as one history step.
A drag that does not start on a handle is what it always was (PROCESSED: drag into FL).
