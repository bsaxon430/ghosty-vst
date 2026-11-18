# Ghosty Pro: CPU hot spots (brief for a later perf pass)

Measured 2026-10-25 with `ProTests bench` (48 kHz, 128-sample blocks, processor only, one core) on an AMD EPYC-Turin
server that other builds were sharing. Quietest runs: INIT 1 voice about 1.5 %, 16 notes x 8 unison x 2 osc at Ultra about
60 % (High 34 %, Draft 17.5 %). The heaviest factory sounds are the 3-layer STACK presets (Fanfare Stack, Stadium Chord Stack,
Sunrise Pluck Stack...) at about 15-25 % for a 6-note chord. All 365 factory sounds average about 5 %.
`bench --check` fails above 2.5 % (INIT 1 voice) or 75 % (16 x 8 x 2 Ultra), each on the quietest of 3 runs.

The per-stage split comes from perf call stacks under `ProProcessor::processBlock` (RelWithDebInfo ProTests,
`bench --only <scenario>`, then `GhostyPro/tests/perf_stages.py`):

```
perf record --call-graph dwarf,16384 -F 500 -o s.data ProTests bench --only big-ultra --secs 5
perf script -i s.data -F comm,tid,time,ip,sym,srcline --inline | python3 GhostyPro/tests/perf_stages.py
```

Nothing below has been optimised yet. Each item says what to measure before and after. A change is only kept if
`ProTests compare` (byte-identical, or within the reference build's own spread) and the `osc` / `quality` numbers stay green.

| Scenario | osc + voice | engine oversampling (decimator) | filter + drive | FX | ceiling / clean |
|---|---:|---:|---:|---:|---:|
| INIT 1 voice | 29 % | 14 % | 2 % | 25 % | 20 % |
| 16 x 8 x 2 Ultra | 88 % | 8 % | 2 % | 0.3 % | 1.3 % |
| 4-note chord + GHOST VERB + HYPER 7 | 31 % | 17 % | 3 % | 40 % | 6.5 % |
| Fanfare Stack, 6 notes (Draft) | 51 % | - | 31 % | 15 % | 1 % |

## 1. Ladder filter `std::tanh` (filter + drive: 31 % of the STACK presets)

`gpro::Ladder::tick` (`ProProcessor.h`, the `u = std::tanh (u)` line) calls libm `tanhf` once per sample per voice per stage
chain. In the Fanfare Stack profile `__expm1f` (inside `tanhf`) alone is 21 % and `__tanhf` 4 %.
Candidate: a fast tanh with |error| <= 1e-4 over the whole input range, e.g. a clamped rational (Pade 7/6) or a
`x / (1 + |x| + 0.28 x^2)`-style curve corrected to 1e-4. It must be smooth and odd, and exactly saturate to +-1.
Check: the Ladder's self-oscillation and drive tests, aliasing (`drive` mode), `compare` within spread (it will not be byte-identical,
so the reference spread must be measured first), and Fanfare Stack CPU before / after.

## 2. FX chain idle bypass (FX: 25 % of INIT 1 voice even with every amount at 0)

`ghosty::FxChain::process` runs its delay lines and filters (`juce::dsp::DelayLine::popSample`, `FxCore.h:218-236`) at zero
amounts. Candidate: skip a unit while its amount (and its target) is 0, but only after its tail has rung out. Keep a
per-unit "silent for N samples" counter (N = the unit's longest delay / decay, e.g. echo feedback time, reverb T60), and on
re-enable start from a cleared state, so no stale tail comes back. The GHOST VERB / echo tails must not be cut when the knob
goes to 0 while a tail is playing. Check: ZIPPER test, `compare`, a knob-to-zero-mid-tail render (no click), and INIT 1 voice CPU.

## 3. Per-unison-voice inner loop (osc + voice: 88 % of 16 x 8 x 2 Ultra)

The hot lines are `ProTables.h:45-49` (`MipFrame::read4`, the 4-point cubic read). It runs 2 times per unison voice per
engine sample (table A and B of the position crossfade) and 4 times during a table fade. The other hot lines are the per-voice
pan / side mixing (`ProProcessor.cpp` around the `U1.sideHpA` / `U2.sideHpA` lines) and `Table::cursor`. Candidates:
- one cubic read of a pre-blended A/B frame when the position is static (cache the blended mip per block);
- SIMD across unison voices (8 voices = one AVX lane set; phases, increments and pans as arrays);
- hoist `U1.pl / U1.pr * lvl1` products out of the sample loop (per block, ramped).
Check: `osc` test numbers unchanged (aliasing / partials), `unison` test, `compare` byte-identical where possible.
(Crash safety, 2026-10-25: `read4` only masks its index. A `floor()` or branches per read cost 15-30 % here, so keep
the inner loop branch-free. The phases are kept in [0, 1) where they advance.)

## 4. Engine oversampling decimator (8-17 %)

`gpro::Decimator::out` (`ProDrive.h:116`) is a plain FIR dot product over `LD` taps per output sample, for left and right,
once per voice. Candidates: a polyphase / half-band structure (every other tap is 0 in a half-band), SIMD over taps, and
decimating the voice sum once instead of each voice where the drive path allows it (the drive is per voice, so only after
the last nonlinearity). Check: the `quality` test (aliasing per quality), the latency report, `compare`.

## 5. Smaller items

- `ghosty::clean::Ceiling::process` (`CleanStage.h:168`, true-peak ceiling): 10 % of INIT 1 voice. It is a fixed cost per
  block; look at the true-peak upsampler length.
- A `sin` call per block inside processBlock (`libc_feholdsetround_sse_ctx`, about 3 % of INIT 1 voice): find and cache it.
- ProTests is built with `GPRO_TAPS=1` (test-only level taps, about 3 % of INIT 1 voice). It is not in the plug-in, so bench
  numbers are slightly pessimistic.
