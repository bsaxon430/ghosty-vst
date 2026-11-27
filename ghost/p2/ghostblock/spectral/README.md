# SPECTRAL FX - the spectral effect rack of Poltergeist 2

    Six Sines engine (+ MORPH's mix)  ->  SPECTRAL FX  ->  plugin output

Runs last on the plugin's main stereo output, in the CLAP wrapper (`src/clap/six-sines-clap.cpp`, every edit marked
`// SPECTRAL FX`). Nothing inside the Six Sines engine changes. With SPECTRAL off the output buffer is not read or
written (bit-identical to the plugin without it, zero latency).

Everything the editor needs is in ONE header: `ghostblock/spectral/spectral-fx.h` (no JUCE, no upstream headers).
The CLAP plugin owns the object (`SpectralFx spectral;`); the editor gets a pointer, like `Morph *`.

## Layout (for the FX page)

```
              +----------------- master: ON (pOn), MIX (pMix), ORDER (pOrder) -----------------+
  synth  ---> |  CHAIN (Spectral Corruptor)                      WARPCORE (spectral inversion)  | ---> out
              |  4 slots: effect, on, mix, 8 params each         on + 9 params                  |
              |  chain wet/dry (pChainMix)                       its own Dry/Wet                |
              |  FFT size / window / overlap / frequency limit                                  |
              +---------------------------------------------------------------------------------+
```

- **ORDER is switchable**: `pOrder` 0 = CHAIN then WARPCORE (default), 1 = WARPCORE then CHAIN. The switch is click-free
  (the rack dips to the dry signal for ~40 ms while it swaps).
- **Inside the chain the order is fixed by the kind of effect**, as in Spectral Corruptor's own rack: first every
  Spectral / Partial effect, in slot order, inside ONE shared STFT; then every Time effect (the GENERIC folder: Reverb,
  Delay, Compressor, Distortion, Utility), in slot order. `runOrder()` returns the slots in the order they really run,
  so the page can number them. (A Time effect in slot 1 and a Spectral effect in slot 2 still run Spectral first.)
- Master MIX is a linear wet/dry over both sections; its dry side is delayed by the chain's latency, so it never combs.
  The chain's own wet/dry is upstream's equal-power mixer (also latency-compensated). WarpCore's Dry/Wet is its own.

## Latency
- 0 when SPECTRAL is off, and 0 when only Time effects and/or WarpCore are used (WarpCore keeps its zero latency).
- `FFT size - 1` samples as soon as SPECTRAL is on and at least one slot holds a Spectral or Partial effect
  (127 ... 16383; default 2047 = 42.6 ms at 48 kHz). Slot bypass does not change it.
- CLAP only lets latency change inside `activate()`. So `latency()` is what the host was told at the last activate;
  when the settings ask for something else (`latencyWanted()`), the plugin calls `host->request_restart()` and the new
  value is reported at the next activate. Until then: turning things OFF stays exactly aligned (the rack pads itself to
  the latency the host knows); turning the first spectral effect ON is heard at once but is late by the unreported
  amount until the host restarts the plugin.

## Host parameters (62, ids are final)

| id | host name | range | default | notes |
| --- | --- | --- | --- | --- |
| 910001 `pOn` | Spectral On | 0 / 1 | 0 | master bypass (0 = bypassed) |
| 910002 `pMix` | Spectral Mix | 0 .. 100 % | 100 | master wet/dry |
| 910003 `pFft` | Spectral FFT Size | 0 .. 7 | 4 | 128, 256, 512, 1024, **2048**, 4096, 8192, 16384. Not automatable (rebuilds the STFT) |
| 910004 `pWindow` | Spectral Window | 0 .. 8 | 0 | Hann, Hamming, Blackman, Rectangular, Blackman-Harris, Gaussian, Flat Top, Welch, Triangle. Not automatable |
| 910005 `pOverlap` | Spectral Overlap | 2 .. 16 x | 4 | hop = round(FFT size / overlap). Not automatable |
| 910006 `pLimit` | Spectral Frequency Limit | 0 .. 6 | 2 | 11025, 16000, **22050**, 24000, 44100, 48000, 96000 Hz: spectral effects leave everything above it alone |
| 910007 `pChainMix` | Spectral Chain Mix | 0 .. 100 % | 100 | the chain section's wet/dry |
| 910008 `pOrder` | Spectral Order | 0 / 1 | 0 | 0 Chain > WarpCore, 1 WarpCore > Chain |
| 910100 + 100*s `pSlotType(s)` | Spectral N Effect | 0 .. 38 | 0 | index into `types()` (0 = empty). s = 0..3, N = s+1 |
| 910101 + 100*s `pSlotOn(s)` | Spectral N On | 0 / 1 | 1 | slot bypass (0 = bypassed), click-free |
| 910102 + 100*s `pSlotMix(s)` | Spectral N Mix | 0 .. 100 % | 100 | the slot's own wet/dry (upstream's "stage mix") |
| 910110+k + 100*s `pSlotParam(s,k)` | Spectral N <parameter name> | 0 .. 1 | - | k = 0..7: the effect's first 8 parameters, normalised. The name, and the text of a value, follow the slot's effect (`paramInfo`, `valueToText`); the plugin tells the host to re-read them when the effect changes |
| 910501 `pWarpOn` | WarpCore On | 0 / 1 | 0 | |
| 910502 `pWarpParam(0)` | WarpCore Warp | 1 .. 256 | 50 | number of bands |
| 910503 `pWarpParam(1)` | WarpCore Freq High | 4000 .. 20010 Hz | 20010 | >= 20000 reads "Full" (= half the sample rate) |
| 910504 `pWarpParam(2)` | WarpCore Scale | 0.1 .. 3 | 1 | filter width per band |
| 910505 `pWarpParam(3)` | WarpCore Poles | 1 .. 8 | 2 | |
| 910506 `pWarpParam(4)` | WarpCore Pitch | -24 .. 24 st | 0 | WarpCore's own parameter; 0 = off (see "Decisions") |
| 910507 `pWarpParam(5)` | WarpCore Pitch Affect | 0 / 1 | 1 | upstream `pitch_affect` ("how the formant is changed") |
| 910508 `pWarpParam(6)` | WarpCore Fill Gap | 0 / 1 | 0 | |
| 910509 `pWarpParam(7)` | WarpCore Freq Mode | 0 .. 3 | 2 | voice: 0 + n, voice: 1 + n, **music: 0 + 2n**, music: 1 + 2n |
| 910510 `pWarpParam(8)` | WarpCore Dry/Wet | 0 .. 1 | 1 | |

All are automatable except FFT size / window / overlap. Slot values are saved in real units, so a saved sound does not
depend on the 0..1 mapping.

## The editor's API (main thread)

```cpp
#include "ghostblock/spectral/spectral-fx.h"
using namespace ghostblock::p2::spectral;

types();            // the catalogue: index 0 = empty, 1..38 = the effects. TypeDesc{ id, name, category, domain, params, liveParams }
categories();       // the eight folders, in display order
warpParams();       // WarpCore's nine ParamDesc;  warpAvailable(), warpSimdName()
// ParamDesc{ id, name, kind (Float/Int/Bool/Choice), min, max, def, step, unit, logScale, choices, hostParam }
//   .toNorm(real) / .fromNorm(0..1) / .text(real) / .parse(text, real) / .clamp(real) / .stepped()

fx.on(), fx.mix(), fx.chainMix(), fx.order(), fx.fftIndex(), fx.windowIndex(), fx.overlap(), fx.limitIndex();
fx.slotType(s), fx.slotOn(s), fx.slotMix(s), fx.slotValue(s, p);     // p = any parameter of the slot's effect, real units
fx.warpOn(), fx.warpValue(k);

// edits, same gesture protocol as Morph::uiEdit (0 begin, 1 value, 2 end). The value is the host's (see the table).
fx.uiEdit(SpectralFx::pMix, 80.0, 1);      fx.uiSet(SpectralFx::pSlotType(0), typeIndex("BinShuffle"));
fx.uiSlotValue(s, p, realValue, type);     // any parameter index: the first 8 also go to the host, the rest are editor-only
fx.serviceMainThread();                    // after an edit that changes structure (effect, FFT size/window/overlap) - builds it now
// In this branch the editor wraps these as Poltergeist2Editor::spectralEdit(id, v, type) (also asks the host to flush).

fx.runOrder();                // slots in the order they run
fx.changeCount();             // changes when the host / a loaded state changed something: refresh the page
fx.inLevel(), fx.outLevel(), fx.slotLevel(s);                // peak meters
fx.spectrum(mags, rate, /*input=*/false);                    // newest FFT magnitudes (left), before / after the spectral slots
fx.latency(), fx.latencyWanted();
```

Effects with more than 8 parameters (only Spectral Equalizer: 40): parameters 9.. are editor-only (`hostParam == false`):
set with `uiSlotValue`, saved with the state (call the editor's `markStateDirty()`), not automatable. The equalizer's
first 8 are its 8 band gains. `liveParams == false` (again only the equalizer) means a change re-makes the module on
the main thread and swaps it in (it holds no audio history, so nothing is lost).

A slot's values reset to the new effect's defaults when its effect changes.

## State
Text section `P2SPECTRAL 1` in the plugin state, after the patch's terminating zero and MORPH's section
(`[patch]\0[P2MORPH ...]\0[P2SPECTRAL ...]\0`). Effects by id, parameters by id, so later versions can add both.
A state without the section (older projects, upstream Six Sines states) = rack off and empty. The Six Sines patch
format (`.sxsnp`) is untouched - and, like MORPH, the rack is therefore NOT part of a `.sxsnp` preset.

## How it is built (threads)
- Settings are atomics. The host's automation (audio thread) and the editor (main thread) both write them.
- Effect objects (Spectral Corruptor modules, the STFT, Partial processors) are made on the MAIN thread
  (`serviceMainThread()`, also reached through `host->request_callback()` when automation asks for a new effect), handed
  to the audio thread through one atomic pointer, and the replaced objects come back to the main thread to be freed.
- The audio thread never allocates or locks: it swaps pointers, pushes numbers into the modules, runs them.
  Processing steps are 64 samples, as upstream's plugin; parameter changes glide (300 ms for the full range), slot bypass and
  slot swaps fade (30 ms), master bypass fades (20 ms).
- Upstream's modules read parameters from a `juce::ValueTree`, which locks and allocates. `sc-shadow.h` compiles the
  unmodified module sources against a tiny fixed property bag instead (inside `namespace scrr::dsp` only).

## Files
| file | what | licence |
| --- | --- | --- |
| `spectral-fx.h/.cpp` | settings, host params, state, the audio shell (bypass, latency, swapping) | project (GPL-3) |
| `sc-engine.h/.cpp` | hosts Spectral Corruptor: adapted from upstream's `SpectralProcessor.cpp` (marked `[SC]`) | **MPL-2.0** |
| `sc-shadow.h` | the parameter bridge described above | project |
| `warpcore-isa*.{h,cpp,inc}`, `warpcore-dispatch.cpp` | one unmodified WarpCore build per instruction set + our CPU pick | project |
| `spectral-panel.h/.cpp` | the minimal SPECTRAL page of this branch | project |
| `tests/` | `p2-spectral-tests`, `p2-render-hash` | project |
| `libs/ghostblock-spectral/` | the vendored upstream sources, licences, provenance | MPL-2.0 / GPL-3 |

## Decisions worth knowing
- **No oversampling.** The rack runs at upstream's "1x" setting (upstream's default is 2x). Reason: exact, integer latency
  and half the CPU. Upstream's own Oversampler is vendored and could be added.
- **Exact window normalisation always** (upstream keeps a "legacy" gain only for its own old sessions).
- **Partial effects** (Stretcher, Stiff String, Centroid, P Grid, F Grid) run upstream's `PartialProcessor::processSlice`
  on every slice of the chain; Slices and Slice Crossfade (rack-wide in upstream) are per effect here. Neighbouring
  Partial slots with the same slicing share the slices as in upstream; other effects work on the summed spectrum
  (upstream clones them per slice).
- **Stretcher** stretches the spacing of partials around a fixed reference (inharmonicity). It is neither time
  stretching nor a transposer, so it is in. Removing it = deleting its name from `folders()` in `sc-engine.cpp`
  (do that only before any release: the effect indices after it would shift).
- **WarpCore Pitch** is one of WarpCore's own nine parameters (default 0 = no change). Exposed because it is part of
  the real effect; say the word and it is pinned to 0 and hidden.
- **Frequency Remap** uses upstream's curve mode with a three-node curve whose coordinates are the parameters
  (identity by default). Upstream's formula-graph mode needs its editor and is not exposed.
- **Spectral Equalizer**: all 40 real parameters are there; 8 gains for the host, the rest editor-only.
- Not exposed: per-effect frequency range (upstream's "range low/high", fixed to everything), input/output gain,
  macros / LFOs / followers (upstream's modulation system), frequency-slice racks (CH 1-4), solo.

## Effects and their parameters
`P` = host parameter (automatable), `x` = editor-only. From `p2-spectral-tests list`:

```
 1  BinShuffle           Bin Shuffle            DESTRUCTION      spectral
      P1  amount             Amount           0 .. 1  default 0
      P2  seed               Seed             0 .. 999999  default 0
 2  RandomBinDeath       Random Bin Death       DESTRUCTION      spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  seed               Seed             0 .. 999999  default 0
 3  SpectralDropout      Spectral Dropout       DESTRUCTION      spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  density            Density          0 .. 100  default 50 %
 4  SpectralPixelation   Spectral Pixelation    DESTRUCTION      spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  blockSize          Block Size       1 .. 64  default 64 bins
 5  SpectralJPEG         Spectral JPEG          DESTRUCTION      spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  blockSize          Block Size       1 .. 64  default 8 bins
 6  SpectralClipper      Spectral Clipper       DESTRUCTION      spectral
      P1  threshold          Threshold        -96 .. 0  default -12 dBFS
      P2  drive              Drive            0 .. 48  default 0 dB
      P3  knee               Knee             0 .. 24  default 0 dB
      P4  output             Output           -48 .. 24  default 0 dB
 7  FrequencyShift       Frequency Shift        FREQUENCY        spectral
      P1  shift              Shift            -5000 .. 5000  default 0 Hz
 8  FrequencyWarp        Frequency Warp         FREQUENCY        spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  shape              Shape            -100 .. 100  default 50
 9  FrequencyDrift       Frequency Drift        FREQUENCY        spectral
      P1  rate               Rate             0.01 .. 10  default 0.25 Hz
      P2  depth              Depth            0 .. 64  default 0 bins
10  FrequencyRemap       Frequency Remap        FREQUENCY        spectral
      P1  amount             Remap            0 .. 100  default 100 %
      P2  curve.lowTo        Low To           0 .. 100  default 0 %
      P3  curve.midFrom      Mid From         0.1 .. 99.9  default 50 %
      P4  curve.midTo        Mid To           0 .. 100  default 50 %
      P5  curve.highTo       High To          0 .. 100  default 100 %
      P6  curve.bendLow      Bend Low         -100 .. 100  default 0 %
      P7  curve.bendHigh     Bend High        -100 .. 100  default 0 %
      P8  remapInvalidPolicy Out Of Range     0 .. 1  default 0 {Silence, Keep Original}
11  SpectralMirror       Spectral Mirror        FREQUENCY        spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  pivot              Mirror Centre    20 .. 10000  default 1000 Hz  (log)
12  BinTeleport          Bin Teleport           FREQUENCY        spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  seed               Seed             0 .. 999999  default 0
13  PhaseCorruption      Phase Corruption       PHASE            spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  seed               Seed             0 .. 999999  default 0
14  PhaseNoise           Phase Noise            PHASE            spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  rate               Rate             0.01 .. 64  default 8 Hz
15  ComplexRotation      Complex Rotation       PHASE            spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  randomAmount       Random Amount    0 .. 1  default 0
      P3  randomRate         Random Rate      0.01 .. 10  default 2 Hz
16  MagnitudePhaseSwap   Magnitude Phase Swap   PHASE            spectral
      P1  amount             Amount           0 .. 100  default 0 %
17  SpectralDispersion   Spectral Dispersion    PHASE            spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  curve              Curve            0 .. 200  default 50
18  BinHold              Bin Hold               TEXTURE / TIME   spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  rate               Rate             1 .. 128  default 8 frames
      P3  length             Length           1 .. 128  default 8 frames
      P4  seed               Seed             0 .. 999999  default 0
19  FrameHold            Frame Hold             TEXTURE / TIME   spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  rate               Rate             1 .. 128  default 8 frames
      P3  length             Length           1 .. 128  default 8 frames
20  TemporalSmear        Temporal Smear         TEXTURE / TIME   spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  length             Length           1 .. 64  default 12 frames
21  TimeDesync           Time Desync            TEXTURE / TIME   spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  magDelay           Mag Delay        0 .. 63  default 1 frames
      P3  phaseDelay         Phase Delay      0 .. 63  default 8 frames
22  SpectralBloom        Spectral Bloom         TEXTURE / TIME   spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  spread             Spread           10 .. 3000  default 150 Hz  (log)
23  SpectralFilter       Spectral Filter        FILTER / UTILITY spectral
      P1  filterType         Type             0 .. 3  default 0 {Low Pass, High Pass, Band Pass, Notch}
      P2  freq               Frequency        20 .. 20000  default 1000 Hz  (log)
      P3  slope              Slope            0 .. 3  default 1 {6 dB/oct, 12 dB/oct, 24 dB/oct, 48 dB/oct}
      P4  q                  Q                0.1 .. 20  default 0.707
      P5  mix                Mix              0 .. 100  default 100 %
24  SpectralComb         Spectral Comb          FILTER / UTILITY spectral
      P1  amount             Depth            0 .. 100  default 0 %
      P2  spacing            Tooth Spacing    20 .. 4000  default 220 Hz  (log)
      P3  offset             Offset           0 .. 100  default 0 %
      P4  width              Tooth Width      2 .. 98  default 35 %
25  SpectralEqualizer    Spectral Equalizer     FILTER / UTILITY spectral (rebuild on change)
      P1  band1Gain          Band 1 Gain      -24 .. 24  default 0 dB
      P2  band2Gain          Band 2 Gain      -24 .. 24  default 0 dB
      P3  band3Gain          Band 3 Gain      -24 .. 24  default 0 dB
      P4  band4Gain          Band 4 Gain      -24 .. 24  default 0 dB
      P5  band5Gain          Band 5 Gain      -24 .. 24  default 0 dB
      P6  band6Gain          Band 6 Gain      -24 .. 24  default 0 dB
      P7  band7Gain          Band 7 Gain      -24 .. 24  default 0 dB
      P8  band8Gain          Band 8 Gain      -24 .. 24  default 0 dB
      x9  band1Freq          Band 1 Frequency 20 .. 20000  default 40 Hz  (log)
      x10 band2Freq          Band 2 Frequency 20 .. 20000  default 100 Hz  (log)
      x11 band3Freq          Band 3 Frequency 20 .. 20000  default 250 Hz  (log)
      x12 band4Freq          Band 4 Frequency 20 .. 20000  default 630 Hz  (log)
      x13 band5Freq          Band 5 Frequency 20 .. 20000  default 1600 Hz  (log)
      x14 band6Freq          Band 6 Frequency 20 .. 20000  default 4000 Hz  (log)
      x15 band7Freq          Band 7 Frequency 20 .. 20000  default 10000 Hz  (log)
      x16 band8Freq          Band 8 Frequency 20 .. 20000  default 16000 Hz  (log)
      x17 band1Q             Band 1 Q         0.1 .. 18  default 0.707107  (log)
      x18 band2Q             Band 2 Q         0.1 .. 18  default 0.707107  (log)
      x19 band3Q             Band 3 Q         0.1 .. 18  default 0.707107  (log)
      x20 band4Q             Band 4 Q         0.1 .. 18  default 0.707107  (log)
      x21 band5Q             Band 5 Q         0.1 .. 18  default 0.707107  (log)
      x22 band6Q             Band 6 Q         0.1 .. 18  default 0.707107  (log)
      x23 band7Q             Band 7 Q         0.1 .. 18  default 0.707107  (log)
      x24 band8Q             Band 8 Q         0.1 .. 18  default 0.707107  (log)
      x25 band1Type          Band 1 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x26 band2Type          Band 2 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x27 band3Type          Band 3 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x28 band4Type          Band 4 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x29 band5Type          Band 5 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x30 band6Type          Band 6 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x31 band7Type          Band 7 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x32 band8Type          Band 8 Shape     0 .. 4  default 0 {Bell, Low Shelf, High Shelf, Low Cut, High Cut}
      x33 band1Enabled       Band 1 On        0 .. 1  default 1
      x34 band2Enabled       Band 2 On        0 .. 1  default 1
      x35 band3Enabled       Band 3 On        0 .. 1  default 1
      x36 band4Enabled       Band 4 On        0 .. 1  default 1
      x37 band5Enabled       Band 5 On        0 .. 1  default 1
      x38 band6Enabled       Band 6 On        0 .. 1  default 1
      x39 band7Enabled       Band 7 On        0 .. 1  default 1
      x40 band8Enabled       Band 8 On        0 .. 1  default 1
26  SpectralContrast     Spectral Contrast      HARMONICS        spectral
      P1  amount             Contrast         -100 .. 100  default 0 %
27  HarmonicSculpt       Harmonic Sculpt        HARMONICS        spectral
      P1  amount             Amount           0 .. 100  default 0 %
      P2  track              Auto Track       0 .. 1  default 1
      P3  fundamental        Fundamental      40 .. 2000  default 220 Hz  (log)
      P4  odd                Odd Harmonics    -48 .. 12  default 0 dB
      P5  even               Even Harmonics   -48 .. 12  default 0 dB
      P6  width              Band Width       5 .. 95  default 30 %
28  HarmonicMatch        Harmonic Match         HARMONICS        spectral
      P1  shape              Shape            0 .. 4  default 0 {Triangle, Square, Saw, Pointy, Sweep}
      P2  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P3  amount             Amount           0 .. 100  default 0 %
      P4  colour             Colour / Sweep   0 .. 100  default 50 %
      P5  track              Auto Track       0 .. 1  default 1
      P6  fundamental        Fundamental      40 .. 2000  default 220 Hz  (log)
      P7  width              Band Width       5 .. 95  default 50 %
29  PartialStretcher     Stretcher              PARTIAL          partial
      P1  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P2  shape              Shape            0 .. 3  default 0 {Sine, Triangle, Saw, Square}
      P3  anchor             Anchor           0 .. 1  default 0 {Band Centre, Energy Centroid}
      P4  amount             Stretch          -48 .. 48  default 0 st
      P5  reference          Reference        20 .. 2000  default 110 Hz  (log)
      P6  slices             Slices           2 .. 64  default 16
      P7  crossfade          Slice Crossfade  0 .. 100  default 0 %
30  PartialStiffString   Stiff String           PARTIAL          partial
      P1  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P2  shape              Shape            0 .. 3  default 0 {Sine, Triangle, Saw, Square}
      P3  anchor             Anchor           0 .. 1  default 0 {Band Centre, Energy Centroid}
      P4  amount             Stiffness        0 .. 100  default 0 %
      P5  reference          Reference        20 .. 2000  default 110 Hz  (log)
      P6  slices             Slices           2 .. 64  default 16
      P7  crossfade          Slice Crossfade  0 .. 100  default 0 %
31  PartialCentroid      Centroid               PARTIAL          partial
      P1  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P2  shape              Shape            0 .. 3  default 0 {Sine, Triangle, Saw, Square}
      P3  anchor             Anchor           0 .. 1  default 0 {Band Centre, Energy Centroid}
      P4  amount             Converge         0 .. 100  default 0 %
      P5  target             Target           20 .. 20000  default 1000 Hz  (log)
      P6  slices             Slices           2 .. 64  default 16
      P7  crossfade          Slice Crossfade  0 .. 100  default 0 %
32  PartialPGrid         P Grid                 PARTIAL          partial
      P1  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P2  shape              Shape            0 .. 3  default 0 {Sine, Triangle, Saw, Square}
      P3  anchor             Anchor           0 .. 1  default 0 {Band Centre, Energy Centroid}
      P4  amount             Quantize         0 .. 100  default 0 %
      P5  pitch              Grid Spacing     0.1 .. 48  default 12 st
      P6  reference          Reference        20 .. 2000  default 110 Hz  (log)
      P7  slices             Slices           2 .. 64  default 16
      P8  crossfade          Slice Crossfade  0 .. 100  default 0 %
33  PartialFGrid         F Grid                 PARTIAL          partial
      P1  mode               Mode             0 .. 1  default 0 {Scale, Resynth}
      P2  shape              Shape            0 .. 3  default 0 {Sine, Triangle, Saw, Square}
      P3  anchor             Anchor           0 .. 1  default 0 {Band Centre, Energy Centroid}
      P4  amount             Quantize         0 .. 100  default 0 %
      P5  pitch              Grid Spacing     1 .. 4000  default 110 Hz  (log)
      P6  slices             Slices           2 .. 64  default 16
      P7  crossfade          Slice Crossfade  0 .. 100  default 0 %
34  Reverb               Reverb                 GENERIC          time
      P1  mode               Space            0 .. 2  default 0 {Plate, Hall, Room}
      P2  decay              Decay            0.1 .. 12  default 2.5 s
      P3  size               Size             0 .. 100  default 60 %
      P4  predelay           Pre-delay        0 .. 200  default 20 ms
      P5  damping            Damping          0 .. 100  default 45 %
      P6  mix                Mix              0 .. 100  default 25 %
35  Delay                Delay                  GENERIC          time
      P1  mode               Mode             0 .. 2  default 0 {Normal, Tap Delay, Ping-Pong}
      P2  sync               Sync             0 .. 1  default 0
      P3  division           Beat             0 .. 20  default 13 {1/64 T, 1/64, 1/64 D, 1/32 T, 1/32, 1/32 D, 1/16 T, 1/16, 1/16 D, 1/8 T, 1/8, 1/8 D, 1/4 T, 1/4, 1/4 D, 1/2 T, 1/2, 1/2 D, 1/1 T, 1/1, 1/1 D}
      P4  time               Time             1 .. 2000  default 300 ms  (log)
      P5  feedback           Feedback         0 .. 95  default 35 %
      P6  mix                Mix              0 .. 100  default 30 %
36  Compressor           Compressor             GENERIC          time
      P1  threshold          Threshold        -60 .. 0  default -18 dB
      P2  ratio              Ratio            1 .. 20  default 4 :1
      P3  attack             Attack           0.1 .. 200  default 10 ms  (log)
      P4  release            Release          10 .. 2000  default 100 ms  (log)
      P5  knee               Knee             0 .. 24  default 6 dB
      P6  makeup             Makeup           0 .. 24  default 0 dB
      P7  mix                Mix              0 .. 100  default 100 %
37  Distortion           Distortion             GENERIC          time
      P1  mode               Shape            0 .. 3  default 0 {Tube, Overdrive, Sin Fold, Lin Fold}
      P2  drive              Drive            0 .. 36  default 6 dB
      P3  bias               Bias             -1 .. 1  default 0
      P4  tone               Tone             200 .. 20000  default 16000 Hz  (log)
      P5  output             Output           -36 .. 12  default -6 dB
      P6  mix                Mix              0 .. 100  default 100 %
38  Utility              Utility                GENERIC          time
      P1  invert             Phase Invert     0 .. 3  default 0 {None, Left, Right, Both}
      P2  mode               Channel Mode     0 .. 2  default 0 {Stereo, Left Only, Right Only}
      P3  swap               Swap L/R         0 .. 1  default 0
      P4  width              Width            0 .. 200  default 100 %
      P5  mono               Mono             0 .. 1  default 0
      P6  gain               Gain             -48 .. 12  default 0 dB
      P7  balance            Balance          -100 .. 100  default 0
WarpCore (FMA)
      warp           Warp           1 .. 256  default 50
      f_high         Freq High      4000 .. 20010  default 20010 Hz
      scale          Scale          0.1 .. 3  default 1
      poles          Poles          1 .. 8  default 2
      pitch          Pitch          -24 .. 24  default 0 st
      pitch_affect   Pitch Affect   0 .. 1  default 1
      fill_gap       Fill Gap       0 .. 1  default 0
      freq_mode      Freq Mode      0 .. 3  default 2
      drywet         Dry/Wet        0 .. 1  default 1
```

## The minimal SPECTRAL page in this branch (and what a restyled FX page has to keep)
`spectral-panel.cpp` is one `juce::Component` (1004 x 444) the editor hosts as its fourth page ("SPECTRAL" tab). The
editor's own changes are five marked `// SPECTRAL FX` spots in `p2-editor.h/.cpp`: the constructor's last argument
(`spectral::SpectralFx *`, may be null = no tab), `spectralEdit(id, value, type)`, `kPages = 4`, the tab button and the
panel's bounds. A restyled page can drop `spectral-panel.*` entirely and keep only these rules:
- a host parameter is changed with `Poltergeist2Editor::spectralEdit(id, realValue, 0 begin / 1 value / 2 end)`; it sets
  the rack, queues the gesture for the host, and asks for the main-thread service that builds a new effect / FFT size;
- a slot parameter beyond the eighth (only effects with more than 8) is editor-only: `fx.uiSlotValue(slot, p, real, type)`
  then `markStateDirty()`;
- redraw from the getters when `fx.changeCount()` moves (automation, state load), not while the mouse is down;
- `fx.latency()` is what the host has been told, `fx.latencyWanted()` what the settings need; they differ until the host
  restarts the plugin.
Screenshots of this page: `poltergeist2-tests all <dir>` writes `13-spectral-empty`, `14-spectral-rack`, `15-spectral-light`.
