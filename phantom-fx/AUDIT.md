# Phantom FX — Phase 0 audit (2026-11-08)

Source of truth for every control below: the Ghost Box 3.1.0 code on branch `main`
(`ghosty-pro/GhostyPro/fx/ProFx.cpp`, `FxBox.h`, `FxBoxUnits2.h`, `FxFatten.h`, `FxEq.cpp`, `FxPatterns.h`, `BendLane.h`,
built with `GPFX_BOX=1`) cross-checked against the manual `ghosty-pro/docs/manuals/ghost-box.md`.
Parameter ids are the old host-parameter ids (`fx_<unit>_<param>`); Phantom FX keeps the same scheme so names stay stable.
"pct" = 0–100 % stored 0..1. Ranges with a centre value are skewed (log-like) around that centre.

Every unit also has **On** (bool, default off; the limiter default on) and **Mix** (pct; GHOST SQUEEZE calls it DEPTH; the
limiter has none). Default mix per unit is listed. Old processing order = the order below (limiter always last).

## The 24 effects — controls and behaviour

### Rhythm

**1 GHOST GATE** (`gate`, mix 100 %) — 16 drawable gate patterns + a pump.
- pattern int 1–16 (1); length choice 1/2 BAR, 1 BAR, 2 BARS (1 BAR); smooth pct (20); time pct "Time Amount" (100).
- Pump: pmode choice OFF, 4 ON FLOOR, HALF-TIME, FAST PULSE, DRAWN, SIDECHAIN (OFF); pdepth pct (70); pshape pct (50);
  plen choice 1/2 BEAT, 1 BEAT, 2 BEATS, 1 BAR (1 BEAT); prel 10–1000 ms c180 (180); patk 0.5–50 ms c4 (4); phold 0–200 ms (0);
  pthr −60..0 dB (−24, SIDECHAIN); ponly bool (on: pattern gate bypassed); plook bool "Sidechain Lookahead" (off; adds 5 ms latency).
- Non-parameter state: 16 patterns of 16 steps + a 512-point drawn curve each (`GatePattern` table/steps), saved in state.
- Behaviour: tempo-synced to host song position; click-free edges (smooth); pump = ducking envelope.

**2 GHOST GRID** (`grid`, mix 100 %) — paint effects on a one-bar step grid.
- pattern int 1–8 (1); steps choice 16 STEPS, 32 STEPS (16).
- Ten rows, each a painted step mask + one knob (pct): loop/LENGTH (35), stutter/RATE (50), scratch/DEPTH (50), reverse/AMOUNT (100),
  tape/"TAPE STOP" SPEED (40), filter/DEPTH (60), crush/AMOUNT (55), delay/FEEDBACK (50), verb/SIZE (60), tonal/PITCH (50).
- Non-parameter state: 8 patterns × 10 rows × 32-bit step masks; factory patterns BREAKDOWN, ROLL, SCRATCHY, TAPE OUT, REVERSO,
  DUB THROWS, TONAL, CHAOS.
- Behaviour: sample-accurate block boundaries on the host grid; each row is its own mini effect on the painted steps.

**3 HAUNT EDIT** (`haunt`, mix 100 %) — hold a pad for a tempo-synced stutter gesture.
- pad int 0–16 (0 = none; the held pad); sync bool "Sync Start" (on); LATCH is a UI/state switch (keeps a pad running).
- 16 pads, each with TYPE (STUTTER, BUILD, GATE, GLITCH, REVERSE, TAPE STOP, LP SWEEP, HP SWEEP, CRUSH, PITCH DROP), SIZE
  (division 1/64 … 2 BARS, `padDivs`) and MOTION (pct). Factory pads: 4 stutters (1/4,1/8,1/16,1/32), BUILD, 3 GATEs, 2 GLITCHes,
  REVERSE, PITCH DROP, TAPE STOP, LP SWEEP, HP SWEEP, CRUSH.
- Behaviour: MIDI-triggerable (manual: pads), latency-free trigger, gestures start on the grid when SYNC START is on.

**4 WARP** (`warp`, mix 100 %) — draw a tempo-synced pitch bend.
- mode choice PITCH, TAPE (PITCH); amount pct (100); length choice 1/2 BAR, 1 BAR, 2 BARS, 4 BARS (1 BAR);
  range choice ±2, ±7, ±12, ±24 ST (±12); formant bool "Keep Formants" (off).
- Non-parameter state: the drawn bend lane (with snap: SNAP OFF, SEMITONE, 5TH + OCT, OCTAVE; grid 1/4 … 1/32).
- Behaviour: PITCH keeps time (pitch shifter), TAPE bends time too (varispeed). Old engine: Signalsmith Stretch 1.4.0. Adds latency.

### Colour

**5 CRACKLE** (`crackle`, mix 100 %) — multiband destroyer.
- bands choice 1/2/3 BANDS (1); xlo 40–2000 Hz c250 (220); xhi 400–12000 Hz c2500 (2800).
- Per band k=1..3: bK_curve choice TUBE, TAPE, DIODE, HARD CLIP, FUZZ, RECTIFY, HALF RECT, FOLD, SINE FOLD, BITCRUSH, DOWNSAMPLE,
  BROKEN SPEAKER, CRACKED, DYING BATTERY (defaults TUBE / FUZZ / CRACKED); bK_drive pct (40); bK_mix pct (100); bK_level −24..+12 dB (0).
- prehp 10–2000 Hz (10); prelp 1k–22k Hz (22k); posthp (10); postlp (22k); box choice OFF, SMALL SPEAKER, 4x12 CAB, TELEPHONE, AM RADIO,
  TIN CAN, CARDBOARD BOX, MEGAPHONE, LAPTOP, WALKIE-TALKIE (OFF); boxmix pct (100); auto bool "Auto Gain" (on); out −24..+12 dB (0).

**6 WRECKER** (`wreck`, mix 100 %) — analog saturation from warm to slammed.
- style choice TAPE, TUBE, CONSOLE, TRANSISTOR, PENTODE, FUZZ, FOLD, CRUSH, DECIMATE (TAPE); drive pct (30); tone −1..+1 (0);
  lowcut 10–1000 Hz c90 (10); highcut 1k–22k c6k (22k); thump / steep / punish bools (off); auto bool (on); out −24..+12 dB (0).

**7 TUBE GLOW** (`tube`, mix 100 %) — vintage tube console warmth.
- drive pct (35); char choice LINE, MIC (LINE); bass −12..+12 dB (0); treble −12..+12 dB (0); out −24..+12 dB (0).

**8 SOUL CRUSH** (`crush`, mix 60 %) — level-smashing grit compressor.
- crush pct (50); dark pct "Darkness" (30); release 5–1000 ms c120 (120); out −24..+12 dB (0).

**9 RETRO** (`retro`, mix 100 %) — vintage colour: noise, wobble, dirt, dust and age.
- magic pct (50); tone −1..+1 (0); out −24..+12 dB (0).
- noise_on bool (on), noise pct (30), ntype choice VINYL, DUST, TAPE HISS, WHITE, PINK, HUM 50, HUM 60, RADIO (VINYL), ntone −1..+1 (0),
  nfollow bool "Noise Follows Input" (off); wobble_on (on), wobble pct (30), flutter pct (30); dist_on (on), dist pct (30),
  dtype choice TAPE, TUBE, CONSOLE, TRANSISTOR, FUZZ (TAPE); digi_on (off), digi pct (30); space_on (off), space pct (30);
  mag_on (off), mag pct (30).

**10 FATTEN** (`fatten`, mix 100 %) — one big knob.
- fat pct (40); mode choice CLASSIC, SUB, BODY, SMASH, SATURATE, STACK, TRANSIENT, SAUSAGE (CLASSIC); color −1..+1 (0); crunch pct (20);
  drive 0–24 dB "Input" (0); punch bool (off); lowclean bool "Low Clean" (off: sub below ~90 Hz skips the drive, stays mono); out −12..+12 dB (0).

**11 BASS LAB** (`basslab`, mix 100 %) — low / mid / high bass tool.
- xlo 40–400 Hz (120); xhi 700–8000 Hz (2200); lowwidth pct (100; below 100 % = mono sub); sub pct (0); growl pct (0); gfreq 300–1500 Hz (750);
  gmove pct (0); gdiv choice (echo divisions, default 1/8); edge pct (0); translate pct (0); low / mid / high −18..+12 dB (0);
  auto bool "Gain Compensation" (on); out −24..+12 dB (0).

**12 GHOST FORMANT** (`formant`, mix 100 %) — talking vowel filter.
- vowel float 0–4 (A=0,E,I,O,U=4; shows "A>E 40%"); mouth pct (50); reso pct (50); shift −12..+12 st (0); drive pct (20);
  move choice STILL, LFO, DRAWN (STILL); mdiv choice (echo divisions, 1/4); mdepth pct (50); mlen choice 1/2 BAR … 4 BARS (1 BAR);
  body pct (30); mshape choice SINE, TRIANGLE, SAW, SQUARE, S&H, ENV (SINE). Non-parameter: the drawn movement lane.

**13 SPIRIT EQ** (`eq`, mix 100 %) — dynamic EQ over a live analyser. 12 bands (`kBands = 12`), ids `fx_eq_<k>_<p>`:
- per band: on, byp, type choice Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt; slope choice
  6/12/18/24/36/48/72/96 dB (12); freq 10–30000 Hz (40,100,250,600,1500,3500,8000,14000,60,300,2000,10000); gain −30..+30 dB; q 0.025–40 (1);
  stereo choice Stereo, Left, Right, Mid, Side; dyn bool; thr −80..0 dB (−24); range −30..+30 dB (−6); ratio 1–20 (2); att 0.1–200 ms (5);
  rel 5–2000 ms (120); auto bool "Auto Threshold"; detect choice Band, Wide; spec bool "Spectral".
- global: fx_eq_out −24..+24 dB; fx_eq_mode Zero Latency, Linear Low, Linear Medium, Linear High (linear modes add latency);
  fx_eq_char Clean, Subtle, Warm; fx_eq_scale −2..+2 "Gain Scale" (1); fx_eq_autogain bool; fx_eq_invert bool "Phase Invert".
- Brief raises the band count to "up to 24": bands 13–24 are added with the same layout (new ids, appended).

**14 PHANTOM FILTER** (`filter`, mix 100 %) — resonant analog multimode filter with rhythm LFO.
- type choice LOW PASS, BAND PASS, HIGH PASS, NOTCH (LP); poles choice 12 dB, 24 dB (24); routing choice SINGLE, SERIES, PARALLEL (SINGLE);
  cutoff 20–20000 Hz c1000 (1200); reso pct (40); spread −4..+4 oct (1); drive pct (20); lfoamt −1..+1 (0);
  lfoshape SINE, TRI, SAW UP, SAW DOWN, SQUARE, S&H, SMOOTH (SINE); lfosync bool (on); lfodiv choice (echo divisions, 1/4); lforate 0.05–20 Hz (1);
  envamt −1..+1 (0); envrel 10–1000 ms (200).
- Brief adds: 30+ filter types (ladder / OB-Xd / K35 / diode …), drawable envelopes. The 4 old types stay the first four choices.

### Space

**15 CHORUS** (`chorus`, mix 50 %) — rate 0.05–5 Hz c0.6 (0.4); depth pct (50); delay 2–30 ms (12); fb pct (0); width pct (100).

**16 CRYSTAL** (`crystal`, mix 40 %) — reverse + pitched granular splices: pitch −24..+24 st (+12); splice 20–600 ms c150 (180);
delay 0–1500 ms c300 (250); fb pct (40); reverse pct (50); spread pct (60).

**17 ECTO ECHO** (`echo`, mix 30 %) — mode choice TAPE, ANALOG, BBD, DIGITAL (TAPE); sync bool (on); time 1–2000 ms c400 (375);
div choice 1/64 … 2/1 incl. T and D (1/8D); fb pct (45); ping bool (on); width pct (100); lowcut 20–2000 Hz (120); highcut 1k–20k (6500);
char pct (50); duck pct (0).

**18 SPECTRAL SPACE** (`space`, mix 30 %) — size pct (50); blend pct (60); decay 0.3–12 s (2.5); tone −1..+1 (0); mod pct (40);
glimmer pct (20); width pct (100); predelay 0–250 ms (20); duck pct (0).

**19 SHIMMER** (`shimmer`, mix 35 %) — size pct (70); decay 0.5–30 s (5); damp pct (40); shimmer pct (50); predelay 0–250 ms (30);
mod pct (50); width pct (100); freeze bool (off).

**20 HAUNTED HALLS** (`halls`, mix 30 %) — mode choice ROOM, HALL, PLATE, CHAMBER, CATHEDRAL, CLOUD, GALAXY, REVERSE, GATED, NONLINEAR,
DARK TAPE (HALL); size pct (60); decay 0.1–60 s (3); predelay 0–500 ms (20); diffusion pct (70); damp pct (40); lowcut 20–1000 Hz (80);
mod pct (40); width pct (100); early pct (50); shimmer pct (0); freeze bool (off).

**21 GHOST SPECTRA** (`spectra`, mix 70 %) — freeze bool; smear pct (30); scatter pct (0); shift −24..+24 st (0); ghost pct (50, keeps the hits);
motion pct (0); mdiv choice (echo divisions, 1/1); tilt −1..+1 (0); hq bool "HQ Bounce"; fsync choice OFF, 1/16, 1/8, 1/4, 1/2, 1 BAR, 2 BARS;
thresh −90..0 dB (−90). Non-parameter: a 64-band painted mask. Adds latency (STFT, 2048 @ 44.1/48k, 75 % overlap in the old engine).

**22 DOORWAY** (`portal`, mix 35 %) — size pct (60); shape choice METALLIC, ORGANIC, HOLLOW, DIFFUSE, CRYSTALLINE (ORGANIC);
move choice STATIC, DRIFT, ROTATE, EXPAND (DRIFT); density pct (70); decay 0.3–30 s (4); transform pct (0); freeze bool; predelay 0–250 ms (20);
tone −1..+1 (0); early pct "Early Reflections" (50). Brief: becomes a convolution morph (up to 8 IRs, IR import, stretch); the old controls stay
and map onto the IR engine (SHAPE = IR family, MOVEMENT = morph motion, TRANSFORM = morph position).

### Master

**23 GHOST SQUEEZE** (`ott`, DEPTH 100 %) — time pct (50); up pct (60); down pct (70); in −12..+12 dB (0); out −12..+12 dB (0).

**24 GHOST LIMITER** (`lim`, always on, always last, no mix) — ceiling −12..0 dB (−0.3); gain 0..+18 dB (0); release 1–500 ms c80 (80).

### Rack-level things the old plugin had (for the Phase 2 port)
Header IN/OUT (−24..+24 dB) and whole-box MIX, in/out meters, SNAPSHOT bank, A/B + A>B, look button (BLACK / PURPLE / LIGHT / GHOST), BYPASS;
tab row RACK, MORPH, MOTION, BRAIN, PERFORM, BOARD, CAPTURE, DNA, SPLITS, FOCUS, PRINT, G-SPACE; unit strip of tiles (drag to reorder, light = on,
right-click = lane A/B); 8 macros (DARKNESS, CHAOS, SIZE, IMPACT, MOVEMENT, WIDTH, DIRT, AIR) with depth/curve per target; PARALLEL + BLEND A|B;
GHOST SCENES bar: 8 slots CLEAN, FAT, DREAM, BROKEN, SPACE, FREEZE, CHAOS, DROP, CUT / MORPH / SPILL, quantise, MORPH TIME, MIDI keys C1–G1;
fixed-latency switch; 12 s tail. Default window 1180×824 (resizable 960×764 … 1900×1244).

### Flags (controls whose intent needs a decision — the build goes on with the reading in brackets)
1. **GHOST SPLIT (unit)** exists in Ghost Box (25 tiles) but is not one of the brief's 24. Dropped; Brandon OK'd it on 2026-11-08 (the
   rack's parallel lanes cover its job).
2. **BRAIN / DNA / FOCUS / BOARD / CAPTURE / PRINT / G-SPACE / MOTION tabs** are rack tools, not effects, and the brief's rack list does not name
   them. [Reading: Phase 2 ports the rack features the brief names (lanes, scenes, snapshots, A/B, modulators, presets); the extra tabs are not
   rebuilt in Phase 2. Their tab row keeps its place in the design so they can come back.]
3. Default window: the brief says port the UI as is (1180×824); the house rule says ≤ 1240×780. [Reading: 1200×760, resizable.]

## Foundations — what was actually found (cloned at these commits, LICENSE read in the clone)

| Foundation | Repo / commit | Licence found | Notes |
|---|---|---|---|
| JUCE 8.0.4 | juce-framework/JUCE `51d11a2b` (tag 8.0.4) | **AGPL-3.0** (or commercial; LICENSE.md) | brief said GPL-3; JUCE 8 is AGPLv3 — fits our AGPL-3 |
| chowdsp_utils | Chowdhury-DSP/chowdsp_utils `e97b826e` | per module (root LICENSE says so). Read from every module header: common modules + buffers/math/simd = BSD-3-Clause; dsp_utils, dsp_data_structures, filters, waveshapers, compressor, eq, reverb, sources, modal_dsp = **GPL-3.0** (brief said BSD-3) | modules: dsp/{buffers, compressor, dsp_utils, eq, filters, math, reverb, simd, sources, waveshapers, modal_dsp} |
| sst-basic-blocks | surge-synthesizer/sst-basic-blocks `5fb60983` | GPL-3.0 | needed by every sst-* header |
| sst-filters | surge-synthesizer/sst-filters `87a171bc` | GPL-3.0 | header-only |
| sst-waveshapers | surge-synthesizer/sst-waveshapers `dd12f31a` | GPL-3.0 | header-only |
| sst-effects | surge-synthesizer/sst-effects `987b39dc` | GPL-3.0 | **include/sst/effects/ actually holds:** Bonsai, Chorus, Delay, Flanger, FloatyDelay, Nimbus(+Impl), Phaser, Reverb1, Reverb2, RotarySpeaker, TreeMonster, delay/{Microgate, ShortDelay, Widener}, distortion/{BitCrusher, Slewer, TreeMonster}, dynamics/{AutoWah, Compressor}, eq/{EqGraphic6Band, EqNBandParametric, MorphEQ, TiltEQ}; plus voice-effects/ (43 headers). No "ensemble" effect: Chorus only. |
| LSP dsp-units | lsp-plugins/lsp-dsp-units `b7a24758` | **LGPL-3.0** (COPYING.LESSER + COPYING) | GNU-make build, needs lsp-common-lib / lsp-dsp-lib / lsp-lltl-lib / lsp-runtime-lib; has dynamics/{Compressor, Expander, Gate, Limiter, AutoGain}, util/{Crossover, LPCrossover, FFTCrossover, Oversampler, Convolver, Delay} |
| Signalsmith Stretch | Signalsmith-Audio/signalsmith-stretch `88061fd4` | MIT | header-only |
| Signalsmith DSP | Signalsmith-Audio/dsp `ca390d28` | MIT | delay.h, envelopes.h, fft.h, filters.h, spectral.h, windows.h, rates.h, curves.h, mix.h |
| Rubber Band | breakfastquay/rubberband `e4296ac8` | GPL-2.0 text in COPYING (source headers say "or later" — confirm at import) | src/{common, faster, finer} |
| Airwindows | airwindows/airwindows `94cb6793` | MIT | 534 plugins under plugins/LinuxVST/src: Tube, Tube2, Console6/7/8…, Pressure4/5/6, DrumSlam, Density/2/3, FathomFive, BassKit, ToTape5–9, Galactic, TapeDust… |
| BYOD | Chowdhury-DSP/BYOD `1cf22b6a` | GPL-3.0 | src/processors/drive: big_muff, centaur, diode_circuits, flapjack, fuzz_machine, hysteresis, junior_b, king_of_tone, mouse_drive, muff_clipper (+tone, other); neural stages need RTNeural (BSD-3) |
| ChowTapeModel | jatinchowdhury18/AnalogTapeModel `604372e4` | GPL-3.0 | Plugin/Source/Processors/{Hysteresis, Loss_Effects, Degrade, Chew, Compression, Input_Filters}; RTNeural for the hysteresis NN |
| ChowMatrix | Chowdhury-DSP/ChowMatrix `40d8e0ef` | **BSD-3-Clause** (brief said GPL-3) | src/dsp delay nodes + tree |
| CloudSeed | ValdemarOrn/CloudSeed `aa546ba3` master = **moved**: only readme/licence; code now at GhostNoteAudio/**CloudSeedCore** `deb21ded` | MIT (both) | DSP/{ReverbController, ReverbChannel, AllpassDiffuser, ModulatedDelay, MultitapDelay, …} plain C++ |
| Mutable Instruments Clouds | pichenettes/eurorack `f4cfa769` (no root LICENSE: README "Code: MIT", every source file carries the MIT header); desktop-ready fork surge-synthesizer/eurorack `84f4f67a` (CMake lib, used by sst-effects Nimbus) | MIT (code) | "Mutable Instruments" / "Clouds" names never in the UI |
| Dragonfly Reverb | michaelwillis/dragonfly-reverb `440ec7b3` | GPL-3.0 (repo); bundles freeverb3 in common/freeverb with its own COPYING (read at import) | hall / room / plate / early-reflection algorithms, DPF UI not taken |
| FFTConvolver | HiFi-LoFi/FFTConvolver `f2cdeb04` | MIT | FFTConvolver, TwoStageFFTConvolver, AudioFFT |
| SpectralSuite | andrewreeman/SpectralSuite `169eb1e8` | **Unlicense / public domain** (brief said GPL-3) | per-bin spectral plugins (BinScrambler, …) |
| PaulXStretch | essej/paulxstretch `b6175e90` | GPL-3.0 with a Section 7 exception | Source/PS_Source = the stretch engine |
| Vital | mtytel/vital `636ca0ef` | GPL-3.0 | compressor is src/synthesis/effects/compressor.{h,cpp} + modules/compressor_module.{h,cpp} |
| vitOTTx | Sakhnovkrg/vitOTTx `738ba9dc` (fork of edgjj/vitOTT) | GPL-3.0 | **exists** (correction, 2026-11-08): Source/vital_dsp/compressor.{h,cpp} = Vital's multiband OTT compressor lifted into a standalone JUCE plugin; primary for GHOST SQUEEZE |
| ZL Equalizer | ZL-Audio/ZLEqualizer `38c7a85d` | AGPL-3.0 | source/dsp/{filter, compressor, splitter, analyzer, fft, …}; needs its own submodules (highway, nlopt, zldsp_fft) |
| ZL Limiter | ZL-Audio/ZLLimiter `f0de37a6` | AGPL-3.0 | exists; source/dsp/{limiter, over_sample, …} |
| Mango | **odoare/Mango** `d0004db5` (the brief's "Mango" is FX-Mechanics' Mango, not a Tilr plugin) | AGPL-3.0-or-later (3 files LGPL-3.0-or-later) | Source/Dsp/{MangoEngine, EffectBase, EffectTypes, Effects/, DurationWeights, OverrideParser, HeldNotes}; depends on FxmeTools |
| Tilr GATE-12 | tiagolr/gate12 `df652453` | GPL-3.0 | src/{PluginProcessor, Globals, Presets, dsp/…} |
| Tilr TIME-12 | tiagolr/time12 `cb86fd61` | GPL-3.0 | same layout |
| Tilr FILT-R | tiagolr/filtr `b42c4e0a` | **AGPL-3.0** (brief said GPL-3) | same layout |
| clap-juce-extensions | free-audio/clap-juce-extensions `6bde1083` | MIT | CLAP wrapper |
| pluginval | Tracktion/pluginval `286684f7` | GPL-3.0 | test tool only, not linked |

Every licence above is compatible with shipping Phantom FX as AGPL-3.0-or-later. Nothing named in the brief is missing except:
an sst-effects "ensemble" (there is none; sst Chorus + our own voice
stacking on it), CloudSeed at its old URL (moved to CloudSeedCore), and Mango under Tilr (it is odoare/Mango).

Full clone listings: `third_party/audit/*.txt`. Licence texts copied from the clones: `third_party/licenses/<name>/`.
