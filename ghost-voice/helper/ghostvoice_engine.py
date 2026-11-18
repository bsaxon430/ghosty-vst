#!/usr/bin/env python3
# Ghost Voice neural engine helper. AGPL-3.0-only, like Ghost Voice (see ../LICENSE, ../NOTICE.md).
#
# Ghost Voice's plug-in / app never loads a neural network itself. It writes a job file and starts this helper, which runs
# Seed-VC's OWN singing conversion (Plachtaa/seed-vc @ 51383ef, GPL-3.0, inference.py, unchanged apart from Brandon's
# Ghost Voice Identity Lab patch, which adds the two-voice CAMPPlus style blend) in a separate Python process.
#   ghostvoice_engine.py status                 -> one JSON line: is the engine installed, which device
#   ghostvoice_engine.py render JOB.json        -> renders every blend in the job; prints progress lines (see below)
#   ghostvoice_engine.py analyze JOB.json       -> measures voice A and voice B with CAMPPlus (no singing): GV_ANALYSIS <json>
#   ghostvoice_engine.py show JOB.json [mps|cpu] -> prints the exact arguments Seed-VC gets for the job's first blend (GV_COMMAND <json>);
#                                                  needs no torch and sings nothing
#   ghostvoice_engine.py measure JOB.json       -> measures takes made by Ghost Voice's other engines (NATURAL / CLASSIC /
#                                                  BEST OF ALL) with the same CAMPPlus: job["measure"] = [[mix, wav], ...];
#                                                  prints GV_IDENTITY <mix> <json> for each (no singing, nothing changed)
# Progress protocol (stdout, one per line):  GV_PROGRESS <0..1> <words>   GV_RESULT <mix> <wav path>   GV_TIME <mix> <s>
#   GV_IDENTITY <mix> <json: cosine of the new take to A, to B, to the performance>   GV_ERROR <words>   GV_DONE
#
# NOVELTY (our feature, on top of the lab's blend; Seed-VC unchanged): the blended CAMPPlus voice vector is turned away from
# BOTH A and B, towards a direction at right angles to everything A and B are (Gram-Schmidt against A and B), picked by
# VARIATION (a seed). NOVELTY turns it out of A and B's plane: 0.5 = 45 degrees, 1 = 90 degrees; its length stays the blend's. Every take is
# measured afterwards with the same CAMPPlus encoder, so the window shows what really came out.
#
# The engine folder (made by install-engine.sh): <engine>/seed-vc (the pinned checkout, patched), <engine>/lab (the lab's
# ghostvoice package, MIT), <engine>/venv (Python 3.10 + Seed-VC's pinned packages), <engine>/seed-vc/checkpoints (models).
#
# Job file (JSON):
#   source       the performance WAV that is re-sung (its melody, timing and words are kept)
#   a, b         lists of WAVs of voice A and voice B (one or more takes each)
#   mixes        list of B weights 0..1 (0 = voice A, 1 = voice B); every one is rendered from the same start noise
#   rights       must be true: the user confirmed the voices are their own or used with consent
#   out_dir      where the WAVs go (mix_000.wav .. mix_100.wav)
#   steps        diffusion steps (Seed-VC default 30)        cfg     inference_cfg_rate (Seed-VC default 0.7)
#   semitones    pitch shift of the performance (Seed-VC's --semi-tone-shift)      auto_f0  Seed-VC's --auto-f0-adjust
#   prompt       "none" (the lab's promptless blend, default) or "nearest" (Seed-VC's normal reference prompt, from A
#                when the mix is below 0.5 and from B otherwise; more of that singer's detail, but it leans to that singer)
#   novelty      0..1 (see NOVELTY)                          variation  which new direction (an integer)
#   seed         start-noise seed (default 1234)             device  "auto" (Apple GPU when it works) or "cpu"
#   UNDER THE HOOD (build 4): Seed-VC's other inference.py options. Each key is written ONLY when it differs from build 3's fixed
#   value, so an untouched job is byte for byte what build 3 wrote (see seedvc_namespace, tests/test_job_unchanged.py):
#   length_adjust  --length-adjust (float, 1.0)     f0_condition  --f0-condition (bool, true: the singing model)
#   fp16           --fp16 (bool; absent = automatic: on for the Apple GPU, off on the CPU)
#   checkpoint, config   --checkpoint / --config (paths; absent = Seed-VC picks its own model for the f0-condition setting)
import json
import math
import os
import sys
import subprocess
import time
import traceback
import types
from pathlib import Path

ENGINE = Path(os.environ.get('GHOSTVOICE_ENGINE', Path.home() / 'Library/Application Support/GhostBlock/Ghost Voice/engine'))


def say(*parts):
    print(*parts, flush=True)


def apple_gpu_works():
    """the Apple GPU is there AND can hold a tensor (virtual Macs report it but cannot allocate)"""
    try:
        import torch
        if not torch.backends.mps.is_available():
            return False
        x = torch.randn(512, 512, device='mps')
        float((x @ x).sum().cpu())
        return True
    except Exception as exc:
        say(f'GV_PROGRESS 0.01 the Apple GPU is not usable here ({type(exc).__name__}); singing on the CPU')
        return False


def cpu_does_half():
    """can this torch run a float16 layer norm on the CPU? (torch 2.2 on Intel Macs can't)"""
    try:
        import torch
        torch.nn.functional.layer_norm(torch.zeros(1, 4, dtype=torch.float16), (4,))
        return True
    except Exception:
        return False


def status():
    seed = ENGINE / 'seed-vc'
    ok = (seed / 'inference.py').is_file() and 'GHOSTVOICE_SEEDVC_FUSION_PATCH' in (seed / 'inference.py').read_text(errors='ignore')
    models = ENGINE / 'seed-vc' / 'checkpoints'
    have = models.is_dir() and any(models.rglob('*.pth'))
    dev = 'unknown'
    try:
        import torch
        dev = 'mps' if apple_gpu_works() else 'cpu'
    except Exception as exc:  # the venv is not there yet
        dev = f'no torch: {exc}'
    say(json.dumps({'installed': bool(ok), 'models_downloaded': bool(have), 'device': dev, 'engine': str(ENGINE)}))
    return 0


def add_novelty(fused, paths_a, paths_b, model, device, novelty, variation):
    import torch
    from ghostvoice.seedvc_style import enroll_campplus_identity
    A, _ = enroll_campplus_identity(paths_a, model, device)
    B, _ = enroll_campplus_identity(paths_b, model, device)
    f = fused.detach().float().reshape(-1)
    basis = []
    for v in (A.reshape(-1).float().to(f.device), B.reshape(-1).float().to(f.device)):
        for e in basis:
            v = v - torch.dot(v, e) * e
        if v.norm() > 1e-6:
            basis.append(v / v.norm())
    g = torch.Generator().manual_seed(int(variation))
    r = torch.randn(f.numel(), generator=g).to(f.device)
    for e in basis:
        r = r - torch.dot(r, e) * e
    r = r / r.norm().clamp_min(1e-8)
    angle = float(min(1.0, max(0.0, novelty))) * math.pi / 2           # NOVELTY 1 = a quarter turn: all of it new
    turned = math.cos(angle) * f / f.norm().clamp_min(1e-8) + math.sin(angle) * r
    out = (turned / turned.norm().clamp_min(1e-8) * f.norm()).reshape(fused.shape).to(fused.dtype)
    cos = lambda x, y: float(torch.nn.functional.cosine_similarity(x.reshape(1, -1).float(), y.reshape(1, -1).float()).item())
    return out, {'novelty': novelty, 'variation': variation, 'cos_to_a': cos(out, A), 'cos_to_b': cos(out, B), 'cos_to_blend': cos(out, f)}


def campplus_alone():
    from ghostvoice.seedvc_style import load_seedvc_campplus
    seed_root = ENGINE / 'seed-vc'
    found = sorted((seed_root / 'checkpoints').rglob('campplus_cn_common.bin')) or [seed_root / 'campplus_cn_common.bin']
    return load_seedvc_campplus(seed_root, found[0], 'cpu')


def measure_take(wav, a, b, perf, model=None):
    """CAMPPlus cosine of a take to voice A (all its takes), voice B and the performance's own singer"""
    import torch
    from ghostvoice.seedvc_style import enroll_campplus_identity, encode_campplus_reference
    model = model or campplus_alone()
    dev = next(model.parameters()).device
    A, _ = enroll_campplus_identity(a, model, dev)
    B, _ = enroll_campplus_identity(b, model, dev)
    C, _ = encode_campplus_reference(wav, model, dev)
    P, _ = encode_campplus_reference(perf, model, dev)
    cos = lambda x, y: float(torch.nn.functional.cosine_similarity(x.float(), y.float(), dim=-1).item())
    return {'cos_a': cos(C, A), 'cos_b': cos(C, B), 'cos_perf': cos(C, P), 'cos_a_b': cos(A, B)}


def analyze(job_path):
    job = json.loads(Path(job_path).read_text())
    seed_root = ENGINE / 'seed-vc'
    if not (seed_root / 'inference.py').is_file():
        say('GV_ERROR The voice engine is not installed.')
        return 4
    sys.path[:0] = [str(seed_root), str(ENGINE / 'lab')]
    say('GV_PROGRESS 0.1 listening to the voices')
    import torch
    from ghostvoice.seedvc_style import enroll_campplus_identity
    model = campplus_alone()
    out = {}
    try:
        A, ra = enroll_campplus_identity([str(p) for p in job['a']], model, 'cpu')
        B, rb = enroll_campplus_identity([str(p) for p in job['b']], model, 'cpu')
    except Exception as exc:
        say(f'GV_ERROR {exc}')
        return 5
    out['cos_a_b'] = float(torch.nn.functional.cosine_similarity(A, B, dim=-1).item())
    out['a'] = {'takes': ra['reference_count'], 'kept': ra['kept_reference_count'], 'seconds': sum(r['duration_seconds'] for r in ra['references'])}
    out['b'] = {'takes': rb['reference_count'], 'kept': rb['kept_reference_count'], 'seconds': sum(r['duration_seconds'] for r in rb['references'])}
    say('GV_ANALYSIS ' + json.dumps(out))
    say('GV_PROGRESS 1.0 done')
    say('GV_DONE')
    return 0


def measure(job_path):
    job = json.loads(Path(job_path).read_text())
    seed_root = ENGINE / 'seed-vc'
    if not (seed_root / 'inference.py').is_file():
        say('GV_ERROR The voice engine is not installed.')
        return 4
    sys.path[:0] = [str(seed_root), str(ENGINE / 'lab')]
    say('GV_PROGRESS 0.1 measuring with CAMPPlus')
    model = campplus_alone()
    items = job.get('measure') or []
    for i, (m, wav) in enumerate(items):
        try:
            say(f'GV_IDENTITY {float(m):.4f} ' + json.dumps(measure_take(str(wav), [str(p) for p in job['a']], [str(p) for p in job['b']], str(job['source']), model)))
        except Exception as exc:
            say(f'GV_IDENTITY {float(m):.4f} ' + json.dumps({'error': str(exc)}))
        say(f'GV_PROGRESS {0.1 + 0.9 * (i + 1) / max(1, len(items)):.3f} measured {round(float(m) * 100)} % B')
    say('GV_DONE')
    return 0


def render(job_path):
    job = json.loads(Path(job_path).read_text())
    if job.get('rights') is not True:
        say('GV_ERROR Tick "I have the rights to these voices" first.')
        return 3
    seed_root = ENGINE / 'seed-vc'
    if not (seed_root / 'inference.py').is_file():
        say('GV_ERROR The voice engine is not installed.')
        return 4
    out = Path(job['out_dir'])
    out.mkdir(parents=True, exist_ok=True)
    import torch
    if job.get('device') == 'cpu' or not apple_gpu_works():
        torch.backends.mps.is_available = lambda: False          # upstream picks cuda > mps > cpu when it is imported
    os.environ.setdefault('PYTORCH_ENABLE_MPS_FALLBACK', '1')
    if not torch.backends.mps.is_available() and not cpu_does_half():
        # build 3, Intel Macs (torch 2.2.2): Seed-VC always loads Whisper in float16, and this torch can't run float16 layer
        # norms on the CPU ("LayerNormKernelImpl not implemented for 'Half'"). Load it in float32 instead: Seed-VC's own
        # code casts Whisper's input to the model's dtype, so nothing else changes; Seed-VC's files stay as they are.
        import transformers
        original_from_pretrained = transformers.WhisperModel.from_pretrained.__func__

        def whisper_float32(cls, *args, **kw):
            if kw.get('torch_dtype') == torch.float16:
                kw['torch_dtype'] = torch.float32
            return original_from_pretrained(cls, *args, **kw)
        transformers.WhisperModel.from_pretrained = classmethod(whisper_float32)
        say('GV_PROGRESS 0.02 this CPU sings in full precision (float32 Whisper)')
    os.chdir(seed_root)                                            # upstream keeps its models in ./checkpoints
    sys.path[:0] = [str(seed_root), str(ENGINE / 'lab')]
    say('GV_PROGRESS 0.02 loading the voice engine (' + ('Apple GPU' if torch.backends.mps.is_available() else 'CPU') + ')')
    import torch
    import inference                                               # Seed-VC's own script (patched by the lab)
    loaded = {}
    original_load = inference.load_models

    def load_once(args):                                           # the models load once for all the blends of this job
        if 'm' not in loaded:
            loaded['m'] = original_load(args)
        return loaded['m']
    inference.load_models = load_once
    novelty, variation = float(job.get('novelty', 0.0)), int(job.get('variation', 1))
    original_fuse = inference.build_fused_style_condition

    def fuse_with_novelty(paths_a, paths_b, campplus_model, device, weight_b=0.5, return_report=False):
        fused, report = original_fuse(paths_a, paths_b, campplus_model, device, weight_b=weight_b, return_report=True)
        if novelty > 0:
            fused, report['novelty'] = add_novelty(fused, paths_a, paths_b, campplus_model, device, novelty, variation)
        loaded['campplus'] = campplus_model
        return (fused, report) if return_report else fused
    inference.build_fused_style_condition = fuse_with_novelty
    mixes = [float(m) for m in job.get('mixes', [0.5])]
    a, b = [str(p) for p in job['a']], [str(p) for p in job['b']]
    fp16 = inference.device.type != 'cpu'                           # the automatic choice (job["fp16"], when given, overrides it)
    for i, m in enumerate(mixes):
        m = min(1.0, max(0.0, m))
        say(f'GV_PROGRESS {0.05 + 0.9 * i / len(mixes):.3f} singing {round(m * 100)} % B')
        tmp = out / f'.render_{i}'
        tmp.mkdir(exist_ok=True)
        args = seedvc_namespace(job, m, a, b, str(tmp), fp16)
        torch.manual_seed(int(job.get('seed', 1234)))            # the same start noise for every blend
        t0 = time.time()
        try:
            inference.main(args)
        except Exception as exc:
            for line in traceback.format_exc().splitlines():      # the whole story, for the log (not a GV_ line)
                say('  ' + line)
            if inference.device.type == 'mps':
                # build 3: on some Macs Seed-VC's code hits an op the Apple GPU can't do (a float64 conversion on macos-15
                # runners): sing the whole job again on the CPU, in a fresh process (upstream picks its device at import)
                say(f'GV_PROGRESS {0.05 + 0.9 * i / len(mixes):.3f} the Apple GPU could not sing this ({type(exc).__name__}); singing on the CPU')
                cpu_job = out / 'job_cpu.json'
                cpu_job.write_text(json.dumps(dict(job, device='cpu')))
                return subprocess.call([sys.executable, str(Path(__file__).resolve()), 'render', str(cpu_job)])
            say(f'GV_ERROR {type(exc).__name__}: {exc}')
            return 5
        wavs = sorted(tmp.glob('*.wav'), key=lambda p: p.stat().st_mtime)
        if not wavs:
            say('GV_ERROR the engine wrote no audio')
            return 6
        dst = out / f'mix_{round(m * 100):03d}.wav'
        wavs[-1].replace(dst)
        for f in tmp.iterdir():
            f.unlink()
        tmp.rmdir()
        say(f'GV_RESULT {m:.4f} {dst}')
        try:
            say(f'GV_IDENTITY {m:.4f} ' + json.dumps(measure_take(dst, a, b, str(job['source']), loaded.get('campplus'))))
        except Exception as exc:
            say(f'GV_IDENTITY {m:.4f} ' + json.dumps({'error': str(exc)}))
        say(f'GV_TIME {m:.4f} {time.time() - t0:.2f}')
    say('GV_PROGRESS 1.0 done')
    say('GV_DONE')
    return 0


def seedvc_namespace(job, m, a, b, output, fp16_auto):
    """the arguments Seed-VC's inference.main() gets for the blend m: Seed-VC's own option names (inference.py's argparse, with the
    lab's patch adding identity_*). Keys the job does not carry have build 3's values, so an untouched job is unchanged."""
    prompt = job.get('prompt', 'none')
    return types.SimpleNamespace(
        source=str(job['source']), target=(a[0] if m < 0.5 else b[0]), output=output,
        diffusion_steps=int(job.get('steps', 30)), length_adjust=float(job.get('length_adjust', 1.0)), inference_cfg_rate=float(job.get('cfg', 0.7)),
        f0_condition=bool(job.get('f0_condition', True)), auto_f0_adjust=bool(job.get('auto_f0', False)), semi_tone_shift=int(job.get('semitones', 0)),
        checkpoint=(job.get('checkpoint') or None), config=(job.get('config') or None), fp16=(bool(job['fp16']) if 'fp16' in job else fp16_auto),
        identity_a=a, identity_b=b, identity_mix_b=m, fused_identity_no_prompt=(prompt != 'nearest'))


def seedvc_command(ns):
    """the same arguments as the command line of Seed-VC's inference.py (what the UNDER THE HOOD page shows)"""
    tf = lambda v: 'True' if v else 'False'
    parts = ['python', 'inference.py', '--source', ns.source, '--target', ns.target, '--output', ns.output,
             '--diffusion-steps', str(ns.diffusion_steps), '--length-adjust', repr(float(ns.length_adjust)),
             '--inference-cfg-rate', repr(float(ns.inference_cfg_rate)), '--f0-condition', tf(ns.f0_condition),
             '--auto-f0-adjust', tf(ns.auto_f0_adjust), '--semi-tone-shift', str(ns.semi_tone_shift)]
    if ns.checkpoint: parts += ['--checkpoint', ns.checkpoint]
    if ns.config: parts += ['--config', ns.config]
    parts += ['--fp16', tf(ns.fp16)]
    for p in ns.identity_a: parts += ['--identity-a', p]
    for p in ns.identity_b: parts += ['--identity-b', p]
    parts += ['--identity-mix-b', '%g' % ns.identity_mix_b, '--fused-identity-no-prompt', tf(ns.fused_identity_no_prompt)]
    return ' '.join(parts)


def show(job_path, device='mps'):
    """prints, for the job's first blend, the arguments Seed-VC would get (no torch, nothing sung): GV_COMMAND <json>"""
    job = json.loads(Path(job_path).read_text())
    a, b = [str(p) for p in job['a']], [str(p) for p in job['b']]
    m = min(1.0, max(0.0, float((job.get('mixes') or [0.5])[0])))
    ns = seedvc_namespace(job, m, a, b, str(Path(job['out_dir']) / '.render_0'), device != 'cpu')
    say('GV_COMMAND ' + json.dumps({'namespace': vars(ns), 'cli': seedvc_command(ns)}, sort_keys=False))
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == 'status':
        return status()
    if len(sys.argv) >= 3 and sys.argv[1] == 'render':
        return render(sys.argv[2])
    if len(sys.argv) >= 3 and sys.argv[1] == 'analyze':
        return analyze(sys.argv[2])
    if len(sys.argv) >= 3 and sys.argv[1] == 'measure':
        return measure(sys.argv[2])
    if len(sys.argv) >= 3 and sys.argv[1] == 'show':
        return show(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'mps')
    say('usage: ghostvoice_engine.py status | render JOB.json | analyze JOB.json | measure JOB.json | show JOB.json [mps|cpu]')
    return 2


if __name__ == '__main__':
    sys.exit(main())
