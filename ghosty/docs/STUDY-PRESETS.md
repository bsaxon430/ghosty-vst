# What the studied Vital presets have in common

Aggregate numbers only (medians, rates, counts) from 506 presets in the free Vital packs Brandon collected (kept private,
never in this repository). Excluded: W.A. Production and Ghosthack (their licences forbid instrument use) and ModeAudio
(its licence limits use to music productions).
No patch, wavetable or setting list from any preset is copied here or into Ghosty Pro. Vital stores envelope times as the
4th root of seconds and cutoff as a MIDI note; the tables show the knob values and, for times, the seconds.

| Category | Presets |
|---|---|
| Bass | 187 |
| Keys | 88 |
| Lead | 85 |
| Pad | 72 |
| Pluck | 17 |
| Other | 14 |
| Experiment | 12 |
| Sequence | 10 |
| SFX | 9 |
| Percussion | 7 |
| Template | 4 |
| Kick | 1 |

## Bass (187 presets)

- Oscillators on: median 2; 1 osc 21%, 2 oscs 37%, 3 oscs 42%; sub on 0%; sampler (noise) on 42%.
- Unison voices per active osc: median 1.0 (1 voice in 53% of oscs, 5+ in 30%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (52%), 7 (15%), 2 (7%); spectral morph type most used: 0 (70%), 3 (6%), 6 (5%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 366, Audio File Source 158, Line Source 40, Wave Folder 8, Wave Window 7, Frequency Filter 6.
- Filter on: 85%; filter 1 models (Vital index): 0:98, 2:15, 1:12, 7:10; cutoff median 52.869 (MIDI note), resonance median 0.191, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 1.0 s, sustain 1.0, release 0.09 s.
- Modulations per preset: median 8 (max 34). Sources: lfo 846, macro_control 594, env 309, velocity 31, random 25, mod_wheel 15.
- Most common routings (kind of source -> kind of target): lfo -> filter_cutoff (141); lfo -> osc_level (106); lfo -> osc_wave_frame (88); lfo -> osc_distortion_amount (84); env -> filter_cutoff (81); macro_control -> filter_cutoff (64); lfo -> osc_spectral_morph_amount (44); macro_control -> modulation_amount (44).
- Modulation depth (|amount|): median 0.33, 75th percentile 0.63.
- Macros wired: median 2 of 4; all four in 25% of presets.
- Effects on: chorus 59% (mix 0.132), compressor 71% (mix 1.0), delay 21% (mix 0.053), distortion 85% (mix 1.0), eq 68%, fx filter 45% (mix 1.0), flanger 11% (mix 0.121), phaser 18% (mix 0.217), reverb 48% (mix 0.186).
- Reverb size median 0.5, decay median -0.872; delay feedback median 0.43; distortion drive median 6.3.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 42%; legato in 22%.

## Keys (88 presets)

- Oscillators on: median 2.0; 1 osc 20%, 2 oscs 43%, 3 oscs 31%; sub on 0%; sampler (noise) on 42%.
- Unison voices per active osc: median 2.0 (1 voice in 49% of oscs, 5+ in 25%); detune knob median 3.909; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (71%), 2 (6%), 1 (5%); spectral morph type most used: 0 (76%), 2 (4%), 4 (3%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 205, Audio File Source 45, Line Source 15, Frequency Filter 8, Wave Folder 4, Wave Window 2.
- Filter on: 90%; filter 1 models (Vital index): 0:50, 6:12, 7:4, 2:4; cutoff median 69.151 (MIDI note), resonance median 0.276, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.002 s, decay 0.976 s, sustain 0.24, release 0.323 s.
- Modulations per preset: median 4.0 (max 59). Sources: macro_control 264, lfo 208, env 162, random 63, velocity 45, stereo 14.
- Most common routings (kind of source -> kind of target): env -> filter_cutoff (45); lfo -> osc_wave_frame (32); env -> osc_level (27); macro_control -> modulation_amount (25); lfo -> filter_cutoff (21); lfo -> osc_tune (20); velocity -> modulation_amount (20); macro_control -> reverb_dry_wet (19).
- Modulation depth (|amount|): median 0.271, 75th percentile 0.5.
- Macros wired: median 0.0 of 4; all four in 19% of presets.
- Effects on: chorus 45% (mix 0.192), compressor 62% (mix 0.545), delay 53% (mix 0.128), distortion 36% (mix 0.457), eq 66%, fx filter 27% (mix 1.0), flanger 12% (mix 0.213), phaser 11% (mix 0.254), reverb 83% (mix 0.266).
- Reverb size median 0.5, decay median 0.72; delay feedback median 0.5; distortion drive median 2.265.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 14%; legato in 1%.

## Lead (85 presets)

- Oscillators on: median 2; 1 osc 28%, 2 oscs 41%, 3 oscs 28%; sub on 0%; sampler (noise) on 47%.
- Unison voices per active osc: median 2.0 (1 voice in 47% of oscs, 5+ in 34%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (65%), 7 (9%), 1 (6%); spectral morph type most used: 0 (83%), 2 (6%), 5 (4%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 191, Audio File Source 38, Line Source 25, Wave Folder 10, Wave Window 9, Frequency Filter 5.
- Filter on: 85%; filter 1 models (Vital index): 0:48, 6:6, 1:4, 5:3; cutoff median 62.168 (MIDI note), resonance median 0.29, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 1.0 s, sustain 0.828, release 0.09 s.
- Modulations per preset: median 6 (max 51). Sources: macro_control 268, lfo 190, env 125, random 35, mod_wheel 14, velocity 13.
- Most common routings (kind of source -> kind of target): env -> filter_cutoff (44); lfo -> filter_cutoff (34); macro_control -> modulation_amount (31); lfo -> osc_wave_frame (26); macro_control -> reverb_dry_wet (25); macro_control -> filter_cutoff (23); macro_control -> delay_dry_wet (22); env -> osc_level (18).
- Modulation depth (|amount|): median 0.298, 75th percentile 0.54.
- Macros wired: median 1 of 4; all four in 25% of presets.
- Effects on: chorus 64% (mix 0.242), compressor 66% (mix 1.0), delay 49% (mix 0.073), distortion 61% (mix 1.0), eq 62%, fx filter 45% (mix 1.0), flanger 9% (mix 0.085), phaser 13% (mix 0.171), reverb 79% (mix 0.25).
- Reverb size median 0.5, decay median 0.352; delay feedback median 0.495; distortion drive median 4.311.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 59%; legato in 36%.

## Pad (72 presets)

- Oscillators on: median 2.0; 1 osc 19%, 2 oscs 39%, 3 oscs 40%; sub on 0%; sampler (noise) on 43%.
- Unison voices per active osc: median 4.0 (1 voice in 31% of oscs, 5+ in 43%); detune knob median 3.533; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (76%), 7 (6%), 4 (6%); spectral morph type most used: 0 (66%), 2 (7%), 1 (5%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 155, Audio File Source 47, Line Source 14, Frequency Filter 7, Wave Folder 5, Wave Window 4.
- Filter on: 93%; filter 1 models (Vital index): 0:42, 6:5, 1:4, 2:4; cutoff median 64.016 (MIDI note), resonance median 0.3, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.247 s, decay 1.0 s, sustain 0.941, release 0.68 s.
- Modulations per preset: median 6.0 (max 38). Sources: lfo 216, macro_control 182, random 70, env 66, velocity 18, mod_wheel 7.
- Most common routings (kind of source -> kind of target): lfo -> filter_cutoff (35); lfo -> osc_level (27); env -> filter_cutoff (25); lfo -> osc_wave_frame (21); macro_control -> filter_cutoff (21); lfo -> osc_unison_detune (14); lfo -> osc_distortion_amount (14); macro_control -> reverb_dry_wet (13).
- Modulation depth (|amount|): median 0.289, 75th percentile 0.51.
- Macros wired: median 0.0 of 4; all four in 17% of presets.
- Effects on: chorus 60% (mix 0.434), compressor 58% (mix 1.0), delay 42% (mix 0.238), distortion 38% (mix 1.0), eq 68%, fx filter 29% (mix 1.0), flanger 11% (mix 0.5), phaser 24% (mix 0.68), reverb 86% (mix 0.358).
- Reverb size median 0.5, decay median 0.883; delay feedback median 0.5; distortion drive median 0.0.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 11%; legato in 3%.

## Pluck (17 presets)

- Oscillators on: median 2; 1 osc 24%, 2 oscs 41%, 3 oscs 29%; sub on 0%; sampler (noise) on 18%.
- Unison voices per active osc: median 5.0 (1 voice in 36% of oscs, 5+ in 58%); detune knob median 2.972; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (64%), 7 (15%), 4 (9%); spectral morph type most used: 0 (82%), 6 (9%), 7 (3%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 39, Audio File Source 12.
- Filter on: 100%; filter 1 models (Vital index): 0:15, 6:1; cutoff median 38.366 (MIDI note), resonance median 0.194, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 0.611 s, sustain 0.01, release 0.318 s.
- Modulations per preset: median 11 (max 17). Sources: macro_control 67, env 51, lfo 30, note 6, random 4.
- Most common routings (kind of source -> kind of target): env -> filter_cutoff (15); macro_control -> reverb_dry_wet (10); lfo -> osc_transpose (10); macro_control -> modulation_amount (8); macro_control -> reverb_decay_time (6); macro_control -> delay_dry_wet (5); lfo -> osc_level (5); macro_control -> osc_unison_detune (5).
- Modulation depth (|amount|): median 0.281, 75th percentile 0.588.
- Macros wired: median 2 of 4; all four in 24% of presets.
- Effects on: chorus 59% (mix 0.252), compressor 76% (mix 1.0), delay 82% (mix 0.128), distortion 71% (mix 1.0), eq 82%, fx filter 41% (mix 1.0), flanger 12% (mix 0.189), phaser 24% (mix 0.47), reverb 100% (mix 0.309).
- Reverb size median 0.5, decay median 0.3; delay feedback median 0.389; distortion drive median -17.18.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 35%; legato in 0%.

## Other (14 presets)

- Oscillators on: median 2.0; 1 osc 14%, 2 oscs 64%, 3 oscs 21%; sub on 0%; sampler (noise) on 43%.
- Unison voices per active osc: median 8.0 (1 voice in 21% of oscs, 5+ in 66%); detune knob median 2.572; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (66%), 3 (10%), 5 (7%); spectral morph type most used: 0 (48%), 10 (14%), 6 (7%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 33, Audio File Source 7, Line Source 2.
- Filter on: 79%; filter 1 models (Vital index): 0:10, 3:1; cutoff median 65.286 (MIDI note), resonance median 0.179, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.001 s, decay 1.0 s, sustain 1.0, release 0.098 s.
- Modulations per preset: median 4.5 (max 16). Sources: env 24, macro_control 24, lfo 21, random 6, velocity 3.
- Most common routings (kind of source -> kind of target): env -> filter_cutoff (7); lfo -> osc_level (4); env -> osc_transpose (4); env -> osc_level (4); macro_control -> filter_mix (3); macro_control -> phaser_dry_wet (3); macro_control -> modulation_amount (3); env -> osc_distortion_amount (3).
- Modulation depth (|amount|): median 0.278, 75th percentile 0.62.
- Macros wired: median 0.5 of 4; all four in 7% of presets.
- Effects on: chorus 64% (mix 0.275), compressor 50% (mix 1.0), delay 36% (mix 0.118), distortion 57% (mix 1.0), eq 79%, fx filter 29% (mix 0.668), phaser 7% (mix 0.0), reverb 57% (mix 0.28).
- Reverb size median 0.5, decay median -0.059; delay feedback median 0.31; distortion drive median 2.339.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 21%; legato in 21%.

## Experiment (12 presets)

- Oscillators on: median 2.0; 1 osc 17%, 2 oscs 67%, 3 oscs 17%; sub on 0%; sampler (noise) on 50%.
- Unison voices per active osc: median 3.5 (1 voice in 21% of oscs, 5+ in 38%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (71%), 5 (8%), 10 (8%); spectral morph type most used: 0 (67%), 6 (8%), 5 (8%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 29, Line Source 4, Audio File Source 3, Wave Folder 1, Wave Window 1.
- Filter on: 100%; filter 1 models (Vital index): 0:5, 6:3, 5:2, 4:1; cutoff median 60.0 (MIDI note), resonance median 0.5, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.068 s, decay 0.878 s, sustain 0.629, release 0.896 s.
- Modulations per preset: median 7.0 (max 12). Sources: lfo 53, env 12, macro_control 8, random 4, velocity 3, note 1.
- Most common routings (kind of source -> kind of target): lfo -> filter_cutoff (9); lfo -> osc_wave_frame (5); lfo -> osc_level (5); lfo -> osc_distortion_amount (4); lfo -> osc_tune (4); lfo -> filter_resonance (3); lfo -> sample_level (3); lfo -> distortion_drive (3).
- Modulation depth (|amount|): median 0.334, 75th percentile 0.592.
- Macros wired: median 0.0 of 4; all four in 0% of presets.
- Effects on: chorus 33% (mix 0.441), compressor 75% (mix 1.0), delay 25% (mix 0.269), distortion 33% (mix 1.0), eq 25%, fx filter 8% (mix 1.0), flanger 17% (mix 0.5), phaser 17% (mix 1.0), reverb 75% (mix 0.308).
- Reverb size median 0.5, decay median 0.0; delay feedback median 0.5; distortion drive median 1.38.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 0%; legato in 0%.

## Sequence (10 presets)

- Oscillators on: median 3.0; 1 osc 0%, 2 oscs 10%, 3 oscs 90%; sub on 0%; sampler (noise) on 80%.
- Unison voices per active osc: median 1.0 (1 voice in 55% of oscs, 5+ in 41%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (62%), 1 (14%), 5 (7%); spectral morph type most used: 0 (76%), 2 (10%), 6 (7%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 19, Audio File Source 11.
- Filter on: 90%; filter 1 models (Vital index): 0:6, 2:1, 7:1, 3:1; cutoff median 36.971 (MIDI note), resonance median 0.265, drive median 3.1, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 1.0 s, sustain 1.0, release 0.09 s.
- Modulations per preset: median 18.0 (max 62). Sources: lfo 111, macro_control 71, random 21, env 11, pitch_wheel 2, stereo 1.
- Most common routings (kind of source -> kind of target): lfo -> osc_transpose (25); lfo -> osc_level (23); macro_control -> modulation_amount (14); lfo -> filter_cutoff (13); lfo -> sample_level (9); lfo -> osc_wave_frame (7); env -> osc_transpose (7); macro_control -> lfo_phase (7).
- Modulation depth (|amount|): median 0.37, 75th percentile 0.834.
- Macros wired: median 3.0 of 4; all four in 40% of presets.
- Effects on: chorus 80% (mix 0.14), compressor 80% (mix 1.0), delay 70% (mix 0.055), distortion 70% (mix 1.0), eq 80%, fx filter 50% (mix 0.57), flanger 10% (mix 0.188), reverb 100% (mix 0.248).
- Reverb size median 0.5, decay median 0.06; delay feedback median 0.41; distortion drive median 1.0.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 30%; legato in 40%.

## SFX (9 presets)

- Oscillators on: median 2; 1 osc 11%, 2 oscs 44%, 3 oscs 33%; sub on 0%; sampler (noise) on 44%.
- Unison voices per active osc: median 3.5 (1 voice in 33% of oscs, 5+ in 50%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (67%), 1 (11%), 7 (11%); spectral morph type most used: 0 (78%), 10 (11%), 4 (6%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 21, Line Source 4, Audio File Source 2, Frequency Filter 1, Slew Limiter 1.
- Filter on: 89%; filter 1 models (Vital index): 5:3, 0:3, 6:1, 3:1; cutoff median 73.467 (MIDI note), resonance median 0.487, drive median 0.0, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 1.0 s, sustain 1.0, release 0.09 s.
- Modulations per preset: median 6 (max 37). Sources: lfo 52, env 12, macro_control 11, stereo 6, random 1.
- Most common routings (kind of source -> kind of target): lfo -> osc_level (6); lfo -> osc_distortion_amount (4); lfo -> filter_cutoff (4); lfo -> osc_unison_detune (4); env -> osc_level (3); lfo -> sample_level (3); lfo -> modulation_amount (3); lfo -> voice_tune (3).
- Modulation depth (|amount|): median 0.388, 75th percentile 0.505.
- Macros wired: median 0 of 4; all four in 11% of presets.
- Effects on: chorus 56% (mix 0.084), compressor 67% (mix 1.0), delay 22% (mix 0.259), distortion 67% (mix 1.0), eq 56%, fx filter 22% (mix 0.5), flanger 22% (mix 0.25), phaser 22% (mix 0.615), reverb 78% (mix 0.25).
- Reverb size median 0.5, decay median 0.0; delay feedback median 0.5; distortion drive median 2.846.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 44%; legato in 33%.

## Percussion (7 presets)

- Oscillators on: median 3; 1 osc 29%, 2 oscs 0%, 3 oscs 57%; sub on 0%; sampler (noise) on 57%.
- Unison voices per active osc: median 2.0 (1 voice in 43% of oscs, 5+ in 14%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (21%), 1 (21%), 9 (21%); spectral morph type most used: 0 (43%), 2 (36%), 3 (21%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 15, Audio File Source 6, Frequency Filter 1.
- Filter on: 57%; filter 1 models (Vital index): 4:2, 6:1, 0:1; cutoff median 101.056 (MIDI note), resonance median 0.11, drive median 1.45, keytrack median 0.0.
- Amp envelope (env 1) medians: attack 0.0 s, decay 0.228 s, sustain 0.0, release 0.09 s.
- Modulations per preset: median 8 (max 29). Sources: macro_control 40, env 18, lfo 17, random 6, mod_wheel 2, note 1.
- Most common routings (kind of source -> kind of target): macro_control -> osc_distortion_amount (9); macro_control -> osc_spectral_morph_amount (7); env -> osc_unison_detune (4); env -> osc_wave_frame (4); env -> filter_cutoff (4); macro_control -> osc_unison_detune (4); lfo -> osc_level (4); macro_control -> env_decay (3).
- Modulation depth (|amount|): median 0.277, 75th percentile 0.571.
- Macros wired: median 4 of 4; all four in 57% of presets.
- Effects on: chorus 29% (mix 0.098), compressor 86% (mix 0.39), delay 29% (mix 0.032), distortion 86% (mix 0.622), eq 71%, fx filter 29% (mix 1.0), flanger 14% (mix 0.481), reverb 86% (mix 0.089).
- Reverb size median 0.5, decay median 0.27; delay feedback median 0.19; distortion drive median 8.884.
- Master volume knob median 5558.804 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 0%; legato in 0%.

## Template (4 presets)

- Oscillators on: median 1.0; 1 osc 100%, 2 oscs 0%, 3 oscs 0%; sub on 0%; sampler (noise) on 0%.
- Unison voices per active osc: median 1.0 (1 voice in 100% of oscs, 5+ in 0%); detune knob median 4.472; stereo spread median 1.0.
- Wave warp type (Vital's list index) most used: 0 (100%); spectral morph type most used: 0 (100%).
- Wavetable building blocks used (Vital component types, counts): Wave Source 12.
- Filter on: 0%; filter 1 models (Vital index): ; cutoff median None (MIDI note), resonance median None, drive median None, keytrack median None.
- Amp envelope (env 1) medians: attack 0.0 s, decay 1.0 s, sustain 1.0, release 0.09 s.
- Modulations per preset: median 0.0 (max 0). Sources: .
- Most common routings (kind of source -> kind of target): .
- Modulation depth (|amount|): median None, 75th percentile None.
- Macros wired: median 0.0 of 4; all four in 0% of presets.
- Effects on: .
- Reverb size median None, decay median None; delay feedback median None; distortion drive median None.
- Master volume knob median 5473.041 (Vital's default 5473 = 0 dB on its scale); polyphony median 8.0; glide on in 0%; legato in 0%.

## Lessons for Ghosty Pro's own presets

(Drawn from the numbers above; every preset of ours is designed from scratch and checked to be far from every studied one.)

- **Two oscillators is the norm, three for basses and pads.** Half of all active oscillators run 1-2 unison voices; big
  unison (5+) is kept for a third of sounds, and plucks are the exception (median 5 voices). Stereo spread stays at full.
- **Basses move.** A bass carries the most modulations of any category (median 8, up to 34), mostly LFOs on the filter cutoff, oscillator level, wavetable
  position and warp amount (wobble and growl), with the amp envelope fast and fully sustained (release under 0.1 s).
  The filter is almost always on (91 %), the analog model, cutoff low (around MIDI 45) with a little drive, then
  distortion, compressor (multiband, full mix) and EQ. Chorus stays low (mix about 0.2).
- **Leads and keys are filtered by an envelope.** "Envelope -> filter cutoff" is the top routing for leads, keys and plucks.
  Leads glide in 44 % of presets, keys never use legato. Keys: decay about 0.7 s to a low sustain (about 0.3), release about
  0.27 s, resonance around 0.5.
- **Pads breathe slowly.** Attack about 0.16 s, release about 1 s, sustain high; LFOs sweep the wavetable position; chorus
  at half mix and reverb about a quarter.
- **Plucks** decay to silence (sustain ~0) in about 0.6 s, the filter starts low (MIDI ~36) and an envelope opens it;
  delay (93 %) and reverb (100 %) are nearly always on, both at low mixes (0.13 / 0.3).
- **Macros** are wired in most sounds (median 2 of 4): filter cutoff, warp amount, reverb and delay mixes, envelope times.
- **Effects** are present but subtle: compressor on in 74-93 % (at full mix), reverb mix 0.18-0.3, delay mix about 0.1.
- **Level** is left at Vital's own 0 dB volume knob in almost every preset; loudness is set by the patch itself.
- **Wavetables** come mostly from Vital's own Wave Source frames, and often from resampled audio (Audio File Source).
  Ghosty Pro's presets build their own tables from Vital's Wave Source with harmonic recipes of our own; no studied table
  is used.

How Ghosty Pro uses this: every Ghosty Pro preset is written from scratch by ghosty/tools/make_presets.py (recipes of our
own, the numbers above only as ranges), its level is fitted on Vital's own volume knob to -1.5 dBTP or lower, and
ghosty/tools/check_distance.py proves each one is far from every studied preset (results in PRESET-DISTANCE.md).
