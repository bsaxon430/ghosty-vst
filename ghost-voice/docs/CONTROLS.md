# Ghost Voice: UNDER THE HOOD controls

The **UNDER THE HOOD** tab (next to SETTINGS) shows every option of **Seed-VC's singing conversion** under Seed-VC's own
name, default and range, and sends each one straight into the job file that `helper/ghostvoice_engine.py` hands to Seed-VC.

- Seed-VC version: the one the installer pins, `Plachtaa/seed-vc @ 51383efd921027683c89e5348211d93ff12ac2a8` (GPL-3.0),
  see `helper/install-engine.sh`.
- Where the list comes from: that version's `inference.py` (the argparse block, lines 412-423, and `load_models`) and
  `app_svc.py` (its Gradio controls with their ranges, and its own argparse).
- NATURAL and CLASSIC are Ghost Voice's own engines, not Seed-VC, so they are **not** on this page.

## Coverage

| | count |
|---|---|
| Seed-VC options (inference.py's 12 + the 2 only app_svc.py has) | **14** |
| Shown on the page | **14 / 14** |
| Live controls, each with its own control | **8**: diffusion-steps, length-adjust, inference-cfg-rate, f0-condition, auto-f0-adjust, semi-tone-shift, checkpoint, fp16 |
| Set by something else, shown read-only | **4**: source, target, output (the cards), config (follows the checkpoint, because a config must belong to its checkpoint) |
| Not used on a Mac, shown with the reason | **2**: `--share` (Gradio web page; never started), `--gpu` (CUDA card number; Macs have none) |

The page prints the same count in its header. 8 + 4 + 2 = 14.

## The table (Seed-VC option, our control, job key)

Defaults and ranges are Seed-VC's. "Ghost Voice start" is what a new session uses (build 3's values).

| Seed-VC option | File | Seed-VC default | Range (app_svc.py) | Ghost Voice start | Our control | Job key (`job.json`) | Python argument the helper passes |
|---|---|---|---|---|---|---|---|
| `--source` | inference.py | `./examples/source/source_s1.wav` | | the performance | PERFORMANCE card | `source` | `source` |
| `--target` | inference.py | `./examples/reference/s1p1.wav` | | voice A's first take below 50 % B, voice B's from 50 % | VOICE A / VOICE B cards + the balance | `a`, `b`, `mixes` | `target` |
| `--output` | inference.py | `./reconstructed` | | a fresh folder per blend | (automatic) | `out_dir` | `output` |
| `--diffusion-steps` | inference.py, app_svc.py | 30 (app_svc.py: 10) | 1 - 200 | 30 | **QUALITY** (knob, range 1 - 200) | `steps` | `diffusion_steps` |
| `--length-adjust` | inference.py, app_svc.py | 1.0 | 0.5 - 2.0 | 1.0 | (only on this page) | `length_adjust` | `length_adjust` |
| `--inference-cfg-rate` | inference.py, app_svc.py | 0.7 | 0.0 - 1.0 | 0.7 | **FOLLOW** (knob) | `cfg` | `inference_cfg_rate` |
| `--f0-condition` | inference.py | False | | **True** (Ghost Voice always sang with the singing model) | (only on this page) | `f0_condition` | `f0_condition` |
| `--auto-f0-adjust` | inference.py, app_svc.py | False (app_svc.py: True) | | False | **MATCH RANGE** (switch) | `auto_f0` | `auto_f0_adjust` |
| `--semi-tone-shift` | inference.py, app_svc.py | 0 | -24 - 24 | 0 | **PITCH** (knob, range -24 - 24) | `semitones` | `semi_tone_shift` |
| `--checkpoint` | inference.py, app_svc.py | None (Seed-VC picks its own) | | None | (only on this page) | `checkpoint` | `checkpoint` |
| `--config` | inference.py, app_svc.py | None (Seed-VC picks its own) | | None | follows the checkpoint | `config` | `config` |
| `--fp16` | inference.py, app_svc.py | True | | automatic (Apple GPU on, CPU off) | the Apple GPU / CPU ONLY switches set the automatic value | `fp16` | `fp16` |
| `--share` | app_svc.py only | False | | not used | | | |
| `--gpu` | app_svc.py only | 0 | | not used | | | |

### One value, one place

QUALITY, FOLLOW, PITCH and MATCH RANGE are not copies. They and their rows on the UNDER THE HOOD page read and write the
same field (`EngineSettings` in `Source/Engine/VoiceEngine.h`: `steps`, `follow`, `semitones`, `autoPitch`), so they cannot
disagree. The page's own options live in the same struct (`lengthAdjust`, `f0Condition`, `fp16`, `checkpoint`, `config`),
so they are saved with the session and stay with a saved voice. TERRAIN moves QUALITY / FOLLOW / PITCH exactly as before.

QUALITY's range is now Seed-VC's 1 - 200 (it was 5 - 100) and PITCH's is -24 - 24 (it was -12 - 12), so the knobs can show
every value the page can set. A session saved by build 3 loads to the same values.

## Default view = build 3's renders

A key is written to the job file **only when it differs from build 3's fixed value**, and the helper falls back to build 3's
value when a key is missing:

| key | written when | helper's value when missing (build 3) |
|---|---|---|
| `length_adjust` | not 1.0 | `1.0` |
| `f0_condition` | not true | `True` |
| `fp16` | not automatic | on for the Apple GPU, off on the CPU |
| `checkpoint`, `config` | not empty | `None` |

So with nothing touched the job file is byte for byte what build 3 wrote, and Seed-VC receives the same arguments. This is
tested, three ways:

1. `tests/GhostVoiceTests.cpp` (`hoodTests`): a copy of build 3's `jobToJson` (from commit `b000452`) is compared with the
   current one on several settings, with the page's options untouched, with build 3's controls changed, with a MEASURE list;
   and the window's UNDER THE HOOD job box is compared with build 3's job.
2. `tests/test_job_unchanged.py`: build 3's argument list for Seed-VC (copied from `ghostvoice_engine.py` at `b000452`) is
   compared with `seedvc_namespace()` for every blend, byte for byte, with the automatic fp16 on and off.
3. `GHOST VOICE START` on the page puts every option back, and the test checks the job is build 3's again.

The saved session of an untouched window also has none of the new properties.

## What is sent to Seed-VC (shown on the page)

The page shows the command line of Seed-VC's `inference.py` for the blend on the balance slider, and the job file. The command
line is built from the same job text the helper reads; the test runs the helper's own `ghostvoice_engine.py show JOB.json`
(no torch, nothing is sung) and checks the page's line is identical for 6 jobs. The last four arguments
(`--identity-a`, `--identity-b`, `--identity-mix-b`, `--fused-identity-no-prompt`) are Brandon's Identity Lab blend patch,
which the installer applies to `inference.py`; they are not Seed-VC's own. Numbers are printed exactly as the job carries
them: FOLLOW 0.70 is a float, so the default reads `0.699999988079071`, which is what build 3 has always sent.

## Models

`--checkpoint` / `--config` offer **Seed-VC's own pick** (empty) plus the models of Seed-VC's two published pairs that are
actually on disk in the engine folder:

| Model | checkpoint | config | needs F0 condition |
|---|---|---|---|
| Singing model (44.1 kHz, F0), what the installer downloads | `DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth` | `config_dit_mel_seed_uvit_whisper_base_f0_44k.yml` | True |
| Speech model (22.05 kHz) | `DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth` | `config_dit_mel_seed_uvit_whisper_small_wavenet.yml` | False |

The installer fetches only the singing model. With `--f0-condition False` and no checkpoint, Seed-VC downloads the speech
model itself the first time (needs internet). The page warns when a model and the F0 setting do not fit. With F0 off,
`--auto-f0-adjust` and `--semi-tone-shift` do nothing (Seed-VC says so), so they are greyed out.

## Ghost Voice's own options (not Seed-VC's, so not on the page)

SINGER DETAIL (`prompt`), NOVELTY and VARIATION (`novelty`, `variation`), the start-noise `seed`, the `device` choice, the
rights tick, and the NATURAL / CLASSIC engines. NOVELTY and the blend are our features around Seed-VC; Seed-VC itself is
unchanged apart from the Identity Lab's blend patch.

## What is not an option

Some numbers in Seed-VC's `inference.py` are constants in the code, not options (the 5 s chunk overlap, the 16-frame
crossfade, the RMVPE pitch threshold 0.03, 30 s context window, 3 quantizers). Seed-VC does not expose them, so neither do we.
