#!/usr/bin/env python3
# Ghost Voice build 4 test: with nothing touched on the UNDER THE HOOD page, the arguments the helper hands to Seed-VC are exactly
# build 3's (b000452). B3_NAMESPACE below is build 3's expression copied verbatim from helper/ghostvoice_engine.py at b000452.
# Also: every Seed-VC option reaches the namespace under Seed-VC's own name, and the command line shown is built from the same
# namespace. Standard library only (no torch): run with  python3 -I tests/test_job_unchanged.py
import importlib.util, json, sys, types
from pathlib import Path

HELPER = Path(__file__).resolve().parent.parent / 'helper' / 'ghostvoice_engine.py'
spec = importlib.util.spec_from_file_location('gv_engine', HELPER)
gv = importlib.util.module_from_spec(spec); spec.loader.exec_module(gv)
fails = 0


def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1


def b3_namespace(job, m, a, b, tmp, fp16):          # build 3, verbatim (render loop of ghostvoice_engine.py @ b000452)
    prompt = job.get('prompt', 'none')
    return types.SimpleNamespace(
        source=str(job['source']), target=(a[0] if m < 0.5 else b[0]), output=str(tmp),
        diffusion_steps=int(job.get('steps', 30)), length_adjust=1.0, inference_cfg_rate=float(job.get('cfg', 0.7)),
        f0_condition=True, auto_f0_adjust=bool(job.get('auto_f0', False)), semi_tone_shift=int(job.get('semitones', 0)),
        checkpoint=None, config=None, fp16=fp16,
        identity_a=a, identity_b=b, identity_mix_b=m, fused_identity_no_prompt=(prompt != 'nearest'))


base = {'source': '/p/song.wav', 'a': ['/v/a1.wav', '/v/a2.wav'], 'b': ['/v/b.wav'], 'mixes': [0.0, 0.25, 0.5, 0.75, 1.0], 'rights': True,
        'out_dir': '/o', 'steps': 30, 'cfg': 0.7, 'semitones': 0, 'auto_f0': False, 'prompt': 'none', 'seed': 1234, 'device': 'auto',
        'novelty': 0.0, 'variation': 1}
variants = [base,
            dict(base, steps=100, cfg=0.35, semitones=-5, auto_f0=True, prompt='nearest', novelty=0.4),
            {k: v for k, v in base.items() if k in ('source', 'a', 'b', 'mixes', 'rights', 'out_dir')}]       # a minimal job: all defaults
for i, job in enumerate(variants):
    a, b = [str(p) for p in job['a']], [str(p) for p in job['b']]
    for fp16 in (True, False):
        same = all(vars(gv.seedvc_namespace(job, m, a, b, '/t', fp16)) == vars(b3_namespace(job, m, a, b, '/t', fp16)) for m in job['mixes'])
        check(same, f'job {i}, fp16 auto = {fp16}: Seed-VC gets build 3\'s arguments for every blend')
        check(json.dumps(vars(gv.seedvc_namespace(job, 0.5, a, b, '/t', fp16))) == json.dumps(vars(b3_namespace(job, 0.5, a, b, '/t', fp16))),
              f'job {i}, fp16 auto = {fp16}: same arguments in the same order, byte for byte')

a, b = base['a'], base['b']
ns = lambda **kw: vars(gv.seedvc_namespace(dict(base, **kw), 0.5, a, b, '/t', True))
check(ns(length_adjust=1.3)['length_adjust'] == 1.3, 'length_adjust reaches --length-adjust')
check(ns(f0_condition=False)['f0_condition'] is False, 'f0_condition reaches --f0-condition')
check(ns(fp16=False)['fp16'] is False and vars(gv.seedvc_namespace(dict(base, fp16=True), 0.5, a, b, '/t', False))['fp16'] is True, 'fp16 overrides the automatic choice both ways')
check(ns(checkpoint='/c/m.pth', config='/c/m.yml')['checkpoint'] == '/c/m.pth' and ns(checkpoint='/c/m.pth', config='/c/m.yml')['config'] == '/c/m.yml', 'checkpoint + config reach --checkpoint / --config')
check(ns(checkpoint='')['checkpoint'] is None, 'an empty checkpoint means Seed-VC\'s own pick (None)')
cli = gv.seedvc_command(gv.seedvc_namespace(base, 0.5, a, b, '/t', True))
check(cli.startswith('python inference.py --source /p/song.wav --target /v/b.wav --output /t --diffusion-steps 30 --length-adjust 1.0 --inference-cfg-rate 0.7 --f0-condition True --auto-f0-adjust False --semi-tone-shift 0 --fp16 True'), 'command line: Seed-VC\'s flags in its own order')
if fails:
    print(f'{fails} FAILED'); sys.exit(1)
print('all ok')
