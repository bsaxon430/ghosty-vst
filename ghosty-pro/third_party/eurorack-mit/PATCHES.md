# Local modifications (GhostBlock / Ghosty Pro ENGINES)

The pristine pinned subset is described in PINNED.txt. Every change below is marked `GHOSTY PATCH` in the file. They let the
engines run inside a plug-in voice: at the voice's oversampled engine rate, one instance per voice, on any host thread.

| file | change | why |
|---|---|---|
| plaits/dsp/dsp.h | `kSampleRate`, `kCorrectedSampleRate`, `a0` are `inline thread_local` values set by `SetSampleRate()`; `kMaxBlockSize` 24 -> 48 | the engines run at 48 / 96 / 192 kHz (QUALITY Draft / High / Ultra at a 48 kHz host); 48-sample blocks at 192 kHz keep the once-per-call smoothing at the module's rate |
| stmlib/utils/random.h, random.cc | the generator state is `inline thread_local` | hosts render on several threads; the wrapper saves / restores it per voice |
| plaits/dsp/oscillator/oscillator.h | `kMaxFrequency` = a quarter of 48 kHz at every engine rate (macro over `MaxFrequency48()`) | the same sound at every QUALITY |
| plaits/dsp/oscillator/variable_saw_oscillator.h, variable_shape_oscillator.h | the pulse-width limit is two samples at 48 kHz | the same sound at every QUALITY (it was two samples at the engine rate) |
| plaits/dsp/oscillator/{oscillator, variable_saw, variable_shape, super_square, formant}_oscillator.h, engine/swarm_engine.h | the start value of the frequency interpolator is the same in Hz at every rate | the oscillators of one engine keep their relative start phase (VA: square + saw sum) at every QUALITY |
| plaits/dsp/engine/fm_engine.cc | the feedback one-pole's coefficient follows the rate (0.05 at 48 kHz) | it runs per (4x) sample |
| plaits/dsp/engine/waveshaping_engine.cc | `Tame()` uses the 48 kHz-normalised pitch | the same sound at every QUALITY |
| plaits/dsp/oscillator/harmonic_oscillator.h | the top harmonics fade by their pitch at 48 kHz (gone at 24 kHz) | the same sound at every QUALITY; no harmonics parked at an oversampled Nyquist |
| plaits/dsp/oscillator/string_synth_oscillator.h | octave shift decided by the pitch at 48 kHz; start frequency in Hz | the same registration at every QUALITY |
| plaits/dsp/engine/grain_engine.cc, chord_engine.cc | carrier-shape taming / divide-down gain by the pitch at 48 kHz | the same sound at every QUALITY |
| plaits/dsp/physical_modelling/string.h | `kDelayLineSize` 1024 -> 4096 | the string keeps its lowest note (about 47 Hz) at 192 kHz |
| plaits/dsp/physical_modelling/string.cc | the dispersion-noise one-pole coefficient follows the rate | it runs per sample |
| plaits/dsp/physical_modelling/string_voice.cc | the pluck noise burst is raised by sqrt (rate / 48 kHz) | its low pass narrows re the rate: the string rings as loud at every rate |
| plaits/dsp/physical_modelling/resonator.cc | each mode's Q and top-mode fade use its pitch at 48 kHz | Q = 1 + f x q with f normalised to the engine rate made the modes decay faster at higher rates |
| plaits/dsp/speech/sam_speech_synth.cc | pitch limit 3 kHz at every rate | the same sound at every QUALITY |
| plaits/dsp/speech/naive_speech_synth.cc | the glottal impulse train is scaled by rate / 48 kHz | an impulse train's harmonics fall as the rate climbs |
| clouds/dsp/grain.h | the grain window's LUT index is clamped just below 1.0 | at the peak (gain == 1) `Interpolate()` read `lut_window[4097]`, one float past the table (ASan, installer #24 crash hunt) |
