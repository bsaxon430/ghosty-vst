"""Tests use deliberately synthetic sine waves and a fake renderer.

Passing these tests DOES NOT mean pretrained Seed-VC or voice C sounds good.
"""
from __future__ import annotations
import json
import sys
from pathlib import Path
import numpy as np
import pytest
import soundfile as sf
from ghostvoice.bench import check_inputs, make_jobs, build_command, run_experiment
from ghostvoice.signal_metrics import analyze_wav, pitch_similarity


def tone(path, freq=220, amp=0.13, seconds=3.0, sr=16000):
    x = np.arange(int(sr*seconds), dtype=np.float64)/sr
    sf.write(path, (amp * np.sin(2*np.pi*freq*x)).astype(np.float32), sr)
    return path


def case_files(tmp_path):
    a = tone(tmp_path/'a.wav', 200)
    b = tone(tmp_path/'b.wav', 310)
    source = tone(tmp_path/'source.wav', 260)
    return a, b, source


def fake_backend(tmp_path, *, fail=False, no_output=False):
    root = tmp_path / 'seed-vc'
    root.mkdir()
    code = '''# GHOSTVOICE_SEEDVC_FUSION_PATCH --identity-mix-b
import sys, argparse, pathlib, soundfile as sf, numpy as np
p=argparse.ArgumentParser()
p.add_argument('--source');p.add_argument('--output');p.add_argument('--identity-mix-b',type=float)
a,_=p.parse_known_args()
'''
    if fail:
        code += 'sys.exit(9)\n'
    elif no_output:
        code += 'print("skipping render on purpose")\n'
    else:
        code += '''pathlib.Path(a.output).mkdir(parents=True,exist_ok=True)
sr=16000
wave = .10*np.sin(2*np.pi*(220+a.identity_mix_b*100)*np.arange(sr*3)/sr)
sf.write(pathlib.Path(a.output)/'render.wav',wave,sr)
'''
    (root/'inference.py').write_text(code)
    return root


def test_signal_metrics_detect_clipping_and_silence(tmp_path):
    wav = tone(tmp_path/'a.wav')
    report = analyze_wav(wav)
    assert report['sample_rate']==16000
    assert report['rms'] > .05
    assert len(report['sha256']) == 64
    sf.write(tmp_path/'quiet.wav', np.zeros(48000), 16000)
    with pytest.raises(ValueError,match='Silent'):
        analyze_wav(tmp_path/'quiet.wav')


def test_inputs_require_separate_a_b_performance(tmp_path):
    a,b,source = case_files(tmp_path)
    checks = check_inputs([a], [b], source)
    assert checks['source']['name']=='source.wav'
    with pytest.raises(ValueError,match='same file content'):
        check_inputs([a], [a], source)
    with pytest.raises(ValueError,match='held-out'):
        check_inputs([a], [b], a)


def test_commands_are_real_seedvc_inference_and_anchor_controls(tmp_path):
    a,b,source = case_files(tmp_path)
    jobs = make_jobs([a],[b],(.25,.5,.75))
    assert len(jobs)==5
    assert jobs[0].a==jobs[0].b
    assert jobs[1].a==jobs[1].b
    cmd = build_command(jobs[3],source,tmp_path/'out',python_executable=sys.executable)
    assert cmd[1]=='inference.py'
    assert cmd[cmd.index('--f0-condition')+1]=='true'
    assert cmd[cmd.index('--fused-identity-no-prompt')+1]=='true'
    assert cmd.count('--identity-a')==1
    assert cmd.count('--identity-b')==1
    assert '--identity-mix-b' in cmd
    with pytest.raises(ValueError):
        make_jobs([a], [b], (0.5, 0.5))


def test_end_to_end_mock_runs_and_builds_blind_study(tmp_path):
    a,b,source=case_files(tmp_path)
    root=fake_backend(tmp_path)
    out=tmp_path/'results'
    manifest=run_experiment(a=[a],b=[b],source=source,seed_root=root,output=out,
                            mixes=(.5,),steps=10,fp16=False)
    assert manifest['status']=='rendered_audio_needs_independent_evaluation'
    assert len(manifest['renders'])==3
    assert all(r['status']=='rendered_unreviewed' for r in manifest['renders'])
    assert all(Path(r['wav']).is_file() for r in manifest['renders'])
    assert (out/'experiment.json').is_file()
    blind=out/'blind_listening'
    assert (blind/'index.html').is_file()
    assert sorted(x.name for x in blind.glob('*.wav'))==['clip_01.wav','clip_02.wav','clip_03.wav']
    assert not (blind/'PRIVATE_answer_key_do_not_share.json').exists()
    mapping=json.loads((out/'PRIVATE_answer_key_do_not_share.json').read_text())
    assert set(mapping['assignment'].values())=={'anchor_a','anchor_b','fusion_050'}
    assert all('naturalness' not in r for r in manifest['renders'])
    with pytest.raises(FileExistsError):
        run_experiment(a=[a],b=[b],source=source,seed_root=root,output=out,mixes=(.5,))


def test_dry_run_no_model_inference_called(tmp_path):
    a,b,source=case_files(tmp_path)
    root=fake_backend(tmp_path,fail=True)
    out=tmp_path/'not_created'
    manifest=run_experiment(a=[a],b=[b],source=source,seed_root=root,output=out,
                            mixes=(.25,.5,.75),dry_run=True)
    assert len(manifest['renders'])==5
    assert not out.exists()
    assert all(r['status']=='planned' for r in manifest['renders'])


def test_model_failure_is_captured_without_false_success(tmp_path):
    a,b,source=case_files(tmp_path)
    root=fake_backend(tmp_path,fail=True)
    out=tmp_path/'failed'
    with pytest.raises(RuntimeError,match='failed'):
        run_experiment(a=[a],b=[b],source=source,seed_root=root,output=out,mixes=(.5,))
    manifest=json.loads((out/'experiment.json').read_text())
    assert manifest['renders'][0]['status']=='renderer_failed'
    assert manifest['status']=='running'  # partial, not completed


def test_no_output_is_not_success(tmp_path):
    a,b,source=case_files(tmp_path)
    root=fake_backend(tmp_path,no_output=True)
    out=tmp_path/'empty'
    with pytest.raises(RuntimeError,match='expected one rendered WAV'):
        run_experiment(a=[a],b=[b],source=source,seed_root=root,output=out,mixes=(.5,))
    manifest=json.loads((out/'experiment.json').read_text())
    assert manifest['renders'][0]['status']=='invalid_output_count'


def test_pitch_proxy_same_synthetic_tone(tmp_path):
    a=tone(tmp_path/'p.wav',225)
    b=tone(tmp_path/'q.wav',225)
    report=pitch_similarity(a,b,max_seconds=3)
    assert report['status']=='ok'
    assert report['median_absolute_cents']<2
