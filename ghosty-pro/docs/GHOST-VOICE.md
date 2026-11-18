# GHOST VOICE (formerly a Ghosty Spectrum page) - what it is, how it works, what it measures

> **2026-11-05: the GHOST VOICE page was taken out of Ghosty Spectrum** (owner: "ghosty spectrum is already a cool product without ghost voice"). Spectrum no longer builds Shared/VoiceFusion* or a GHOST VOICE page; a Spectrum project saved with one still opens (that part is ignored). This document describes the engine as it was built, for the standalone Ghost Voice.

Built 2026-11-01 for the owner's brief: "a program that extracts the voice information from a singer ... add those two together to create
a strange voice with qualities you've never heard before." Classic DSP, no models, no GPU; renders on a background thread on a Mac.

## In plain words
1. You sing your performance (**YOUR VOICE**, slot A). Its pitch, timing, words and vibrato are kept exactly.
2. You add **VOICE B** (and optionally **C**): a recording you have the rights to, a pad / choir / instrument, or a **designed voice**
   (Glass Angel, Midnight, Electric Soul, Dream Machine, Velvet, Tiny, Giant, Alien, Choir Of One, Static Ghost - built from formant
   tables, nobody's recording).
3. The page extracts each voice's **fingerprint**, blends the fingerprints at the percentages you set, and sings your take with the blend.
4. **VOICE DESIGN** shapes the new voice, **PITCH** tunes / transposes it (the size never follows the transpose), **STACK** makes a
   ghost choir. **SAVE VOICE** keeps the blend as a preset; exports go through the demo gate like every other page.

## Method (what was chosen and why)
Rule followed: official / open-source first. The only library is **WORLD v1.0.1** (M. Morise, modified BSD; already in the tree for
the VOICE page): Harvest f0, CheapTrick spectral envelope, D4C aperiodicity, and WORLD's pulse + noise synthesis. Everything else is our
own code written from the published methods (nothing copied; see THIRD_PARTY_LICENSES.md):

| step | what | reference |
|---|---|---|
| analysis | WORLD f0 + spectral envelope + aperiodicity per 5 ms frame (the VOICE page's engine, now `Shared/VoiceEngine`) | Morise 2016 (WORLD) |
| fingerprint ("identity") | voiced frames clustered by vowel (k-means on log F1 / F2 / F3); per cluster the mean log envelope on a log-frequency grid (60 Hz - 16 kHz, 256 points, level removed), formants F1-F4, breath in 8 bands, tilt | codebook mapping: Abe, Nakamura, Shikano & Kuwabara 1988 |
| formants | an LPC (order 14) fit of the envelope below 5.5 kHz, merged with the envelope's own peaks (separates F1 / F2 of open vowels that CheapTrick smooths together) | Makhoul 1975 |
| matching | for each frame of your take: soft nearest neighbours (top 3) of the identity's vowels on the formant pattern + envelope shape, smoothed over ~20 ms so the identity never flickers syllable to syllable | - |
| morph | target formants = log-domain weighted mean; each voice's envelope is **warped along frequency so its formants land on the targets** (piecewise-linear dynamic frequency warping anchored at the paired formants), then the warped log envelopes are averaged by the blend weights (log-spectral interpolation) | Pfitzinger 2004; Shechtman & Sorin 2010; Kawahara & Matsui 2003 (STRAIGHT morphing); Slaney, Covell & Lassiter 1996; Paliwal 1995 on which domains interpolate well |
| why not a plain average | averaging two envelopes keeps BOTH sets of peaks: two singers at once, hollow and phasey at 50 / 50. The test `50 % morph of 500 Hz + 1 kHz peaks` shows one peak at 707 Hz with the warp and two peaks without it | - |
| consonants | unvoiced frames keep your own envelope and breath (KEEP MY CONSONANTS, ramped over 15 ms); above 5 kHz the blend leans back to you (CLARITY); your own attacks are spliced back over the onsets above 2.5 kHz | - |
| synthesis | WORLD streaming synthesis with your f0 track (TUNE / transpose applied to the track only), the blended envelope at your frame level, blended aperiodicity | Morise 2016 |

Not used: RMVPE (neural pitch tracker; its licence was not needed - the measurements use WORLD's Harvest plus our own YIN-style tracker
written from de Cheveigne & Kawahara 2002 as a second opinion). No datasets, no recordings of anyone in the tree, tests or presets.

## The numbers (GhostVoiceTests, synthesised voices: A = adult-male vowel table singing a 7-note phrase with vibrato, B = a different
singer on the female table singing other notes and vowels; 48 kHz, the same at 44.1 kHz)

| blend (B %) | F0 RMSE vs the take (Harvest on both) | own YIN tracker | pitch-contour correlation | voiced / unvoiced agreement | timing drift | F1 / F2 position between A (0) and B (1) | identity consistency |
|---|---|---|---|---|---|---|---|
| 0 | 0.9 c | 0.7 c | 1.0000 | 98.3 % | 0 ms | 0.20 / 0.03 | - |
| 25 | 1.3 c | 1.3 c | 1.0000 | 98.3 % | 0 ms | 0.27 / 0.22 (want 0.25) | median 0.12, 80 % of the vowels within 0.25 |
| 50 | 2.0 c | 2.1 c | 1.0000 | 98.5 % | 0 ms | 0.42 / 0.63 (want 0.50) | median 0.43, 80 % within 0.25 |
| 75 | 2.7 c | 2.6 c | 0.9999 | 98.0 % | 0 ms | 0.63 / 0.77 (want 0.75) | median 0.68, 80 % within 0.25 |
| 100 | 3.4 c | 3.1 c | 0.9999 | 97.8 % | 0 ms | 0.81 / 1.12 | - |

- Clicks: the largest sample-to-sample step of every blend is below the source's own (0.07-0.09 vs 0.13).
- A plain-words reading: the pitch is yours to within a few cents (a semitone is 100), the melody shape is identical, the voiced / unvoiced
  pattern agrees 98 % of the time, nothing moves in time, and the vowel resonances of the blend sit between yours and B's roughly where
  the slider says (the formant estimator's own scatter is about +-0.2 of the way).
- Consistency is measured against the engine's own 0 % and 100 % renders (the estimator's per-vowel bias cancels): the median position
  is within 0.07 of the slider and 80-100 % of the (note, formant) measurements lie within 0.25 of it, so the blend does not wander
  between A and B from note to note.
- Also tested: transposes keep the formants where the blend put them; FORMANT +-4 st moves them; HARD tune lands 80 %+ of the frames on
  the key; STACK 4 + octave down gives a stereo result (L/R correlation 0.70); 3-way blends; START / END; cancel; SAVE VOICE round trip;
  the recorder's 4-beat count-in starts the take exactly on the downbeat (2.000 s at 120 bpm) and records the input sample-exact;
  state round trips; the page paints in BLACK / PURPLE / LIGHT; every control has a tooltip; the export gate refuses in demo mode.

## What it sounds like and the limits
- At 25-75 % the result is one voice with your performance; the designed voices are the clearest "new singer" (they have no breath
  noise or room of their own).
- A recorded B works best as 10-60 s of solo singing covering several vowels. Music with chords or a pad gives a "colour" rather than
  vowels (the page says "no clear vowels found - it will be used by its overall colour").
- It is a vocoder-class resynthesis: very breathy or heavily distorted takes come back smoother than the original; GRIT and BREATH add
  texture back. The render is offline (a few seconds for a 30 s take); real-time preview is a later step.

## Files
- `ghosty-pro/Shared/VoiceEngine.{h,cpp}` (moved from SpectrumX; the VOICE page is unchanged, its demo renders are bit-identical)
- `ghosty-pro/Shared/VoiceFusion.{h,cpp}` - the fusion engine; `Shared/VoiceFusionHost.{h,cpp}` - slots, worker, undo, recorder, preview, presets, state
- `ghosty-pro/SpectrumX/GhostVoicePage.{h,cpp}` - the page; `SpecTabs.h`, `SpecEditor.cpp`, `SpecProcessor.*`, `SpecGuide.cpp`, `Voice.cmake`
- `ghosty-pro/SpectrumX/tests/GhostVoiceTests.cpp` (`GhostVoiceTests`, `GhostVoiceTests shots DIR`, `GhostVoiceTests demos DIR`)
- docs: this file, `docs/manuals/ghosty-spectrum.md` (GHOST VOICE + Reference: GHOST VOICE), `docs/SPECTRUM-QUICKSTART.md`, `docs/DEMO-EXPORT-GATE.md` (28 paths)
