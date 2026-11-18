"""Reproducible singing experiment runner for a locally patched Seed-VC backend.

Unlike a fake 'CREATE VOICE' demo, this invokes the full pretrained inference
script and only reports success after a non-silent WAV is actually produced.
No pretrained weights, GPL code, or generated audio are included in this repo.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
import json
import os
import shutil
import subprocess
import sys
import time
from .signal_metrics import analyze_wav, pitch_similarity

PROJECT_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class RenderJob:
    job_id: str
    a: tuple[Path, ...]
    b: tuple[Path, ...]
    weight_b: float


def make_jobs(a: Iterable[str | Path], b: Iterable[str | Path], mixes=(0.25, 0.5, 0.75)) -> list[RenderJob]:
    paths_a = tuple(Path(x).expanduser().resolve() for x in a)
    paths_b = tuple(Path(x).expanduser().resolve() for x in b)
    if not paths_a or not paths_b:
        raise ValueError('Need at least one Voice A reference and one Voice B reference')
    fractions = [float(m) for m in mixes]
    if not fractions or any(not 0 < m < 1 for m in fractions):
        raise ValueError('All candidate mixes must be between 0 and 1 (exclusive)')
    if len(set(fractions)) != len(fractions):
        raise ValueError('Mix values must be unique')
    jobs = [
        RenderJob('anchor_a', paths_a, paths_a, 0.5),
        RenderJob('anchor_b', paths_b, paths_b, 0.5),
    ]
    for mix in fractions:
        jobs.append(RenderJob(f'fusion_{round(mix*100):03d}', paths_a, paths_b, mix))
    return jobs


def check_inputs(a: Iterable[str | Path], b: Iterable[str | Path], source: str | Path) -> dict:
    source = Path(source).resolve()
    paths_a = [Path(p).resolve() for p in a]
    paths_b = [Path(p).resolve() for p in b]
    checks = {'source': analyze_wav(source), 'references_a': [analyze_wav(p) for p in paths_a],
              'references_b': [analyze_wav(p) for p in paths_b]}
    hashes_a = {item['sha256'] for item in checks['references_a']}
    hashes_b = {item['sha256'] for item in checks['references_b']}
    if hashes_a & hashes_b:
        raise ValueError('A and B include the exact same file content; choose distinct singer references')
    if checks['source']['sha256'] in (hashes_a | hashes_b):
        raise ValueError('The source performance duplicates a reference; use a held-out performance')
    checks['warnings'] = (
        ['Digital clipping found in some inputs']
        if any(x['warnings'] for x in [checks['source'], *checks['references_a'], *checks['references_b']])
        else []
    )
    return checks


def ensure_backend(seed_root: Path) -> Path:
    script = seed_root.resolve() / 'inference.py'
    if not script.is_file():
        raise FileNotFoundError(f'Seed-VC inference script not found: {script}')
    source = script.read_text(encoding='utf-8')
    if 'GHOSTVOICE_SEEDVC_FUSION_PATCH' not in source or '--identity-mix-b' not in source:
        raise RuntimeError('Seed-VC not patched. Run patches/apply_seedvc_fusion_patch.py on a separate research checkout.')
    return script


def build_command(job: RenderJob, source: Path, output: Path, *, python_executable: str,
                  steps: int = 40, fp16: bool = True, config: str | None = None,
                  checkpoint: str | None = None) -> list[str]:
    if not 1 <= steps <= 200:
        raise ValueError('Diffusion steps must be between 1 and 200')
    cmd = [python_executable, 'inference.py', '--source', str(source),
           '--target', str(job.a[0]), '--output', str(output),
           '--f0-condition', 'true', '--fused-identity-no-prompt', 'true',
           '--identity-mix-b', str(job.weight_b), '--diffusion-steps', str(steps),
           '--fp16', 'true' if fp16 else 'false']
    for wav in job.a:
        cmd += ['--identity-a', str(wav)]
    for wav in job.b:
        cmd += ['--identity-b', str(wav)]
    if config is not None:
        cmd += ['--config', str(Path(config).resolve())]
    if checkpoint is not None:
        cmd += ['--checkpoint', str(Path(checkpoint).resolve())]
    return cmd


def write_json(path: Path, content: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(content, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def run_experiment(*, a: Iterable[str | Path], b: Iterable[str | Path], source: str | Path,
                   seed_root: str | Path, output: str | Path, mixes=(0.25, 0.5, 0.75),
                   steps: int = 40, fp16: bool = True, dry_run: bool = False,
                   python_executable: str = sys.executable, config: str | None = None,
                   checkpoint: str | None = None, with_pitch: bool = False,
                   timeout_seconds: int = 7200) -> dict:
    """Return/save experiment manifest. Fail loudly if the renderer produces no WAV."""
    jobs = make_jobs(a, b, mixes)
    source_path = Path(source).expanduser().resolve()
    root = Path(seed_root).expanduser().resolve()
    out = Path(output).expanduser().resolve()
    checks = check_inputs(jobs[0].a, jobs[1].b, source_path)
    ensure_backend(root)
    if not 1 <= steps <= 200:
        raise ValueError('Diffusion steps must be between 1 and 200')
    if not dry_run:
        # Avoid overwriting benchmark evidence; create a new output directory per run.
        if out.exists() and any(out.iterdir()):
            raise FileExistsError(f'Nonempty experiment destination: {out}')
    manifest = {
        'schema_version': 2,
        'status': 'dry_run_only' if dry_run else 'running',
        'research_backend': 'GPL-3.0 Seed-VC V1 SVC with Ghost Voice experimental adapter',
        'source_performance': str(source_path),
        'source_not_used_as_identity_reference': True,
        'check_inputs': checks,
        'weights_are_candidate_tests_not_verified_new_singers': list(mixes),
        'seed_vc_root': str(root), 'steps': steps, 'fp16': fp16,
        'warnings': [
            'No claim of novel identity, intelligibility, commercial compatibility or naturalness is possible without listening.',
            'Anchors A and B are rendered from the SAME performance using the SAME promptless pipeline for fair comparison.',
            'The benchmark does not ship Seed-VC or any pretrained model weights.',
            'Pitch proxy may not be meaningful for breathy or noisy recordings.',
        ],
        'renders': [],
    }
    for job in jobs:
        folder = out / job.job_id
        cmd = build_command(job, source_path, folder, python_executable=python_executable,
                            steps=steps, fp16=fp16, config=config, checkpoint=checkpoint)
        item = {'job_id': job.job_id, 'weight_b': job.weight_b, 'command': cmd,
                'status': 'planned' if dry_run else 'running'}
        manifest['renders'].append(item)
        if dry_run:
            continue
        folder.mkdir(parents=True, exist_ok=False)
        env = os.environ.copy()
        env['PYTHONPATH'] = str(PROJECT_ROOT) + os.pathsep + str(root) + os.pathsep + env.get('PYTHONPATH', '')
        started = time.monotonic()
        try:
            p = subprocess.run(cmd, cwd=root, text=True, capture_output=True,
                               timeout=timeout_seconds, env=env, check=False)
        except subprocess.TimeoutExpired as exc:
            item['status'] = 'timeout'
            item['error'] = str(exc)
            write_json(out / 'experiment.json', manifest)
            raise RuntimeError(f'{job.job_id} renderer timed out') from exc
        item['seconds'] = round(time.monotonic()-started, 2)
        (folder / 'inference.log').write_text(p.stdout + '\n===STDERR===\n' + p.stderr, encoding='utf-8')
        item['log'] = str(folder / 'inference.log')
        if p.returncode != 0:
            item['status'] = 'renderer_failed'
            item['returncode'] = p.returncode
            write_json(out / 'experiment.json', manifest)
            raise RuntimeError(f'{job.job_id} failed (code {p.returncode}). See {item["log"]}')
        wavs = sorted(folder.glob('*.wav'))
        if len(wavs) != 1:
            item['status'] = 'invalid_output_count'
            write_json(out / 'experiment.json', manifest)
            raise RuntimeError(f'{job.job_id}: expected one rendered WAV, got {len(wavs)} in {folder}')
        try:
            item['audio'] = analyze_wav(wavs[0])
        except (ValueError, FileNotFoundError) as exc:
            item['status'] = 'invalid_output_audio'
            item['error'] = str(exc)
            write_json(out / 'experiment.json', manifest)
            raise
        item['wav'] = str(wavs[0])
        item['status'] = 'rendered_unreviewed'
        if with_pitch:
            try:
                item['pitch_proxy'] = pitch_similarity(source_path, wavs[0])
            except Exception as exc:
                item['pitch_proxy'] = {'status': 'not_available', 'reason': str(exc)}
        write_json(out / 'experiment.json', manifest)
    if not dry_run:
        manifest['status'] = 'rendered_audio_needs_independent_evaluation'
        create_blind_listening(out, manifest)
        write_json(out / 'experiment.json', manifest)
    return manifest


def create_blind_listening(out: Path, manifest: dict, *, seed: int = 20261105) -> Path:
    """Distribute anonymized anchored A/B/C examples for blind listening.

    The secret identity key is stored in a separate file; do not give it to
    evaluators. Reports are ratings from people, not proof of identity novelty.
    """
    import random
    items = manifest['renders']
    required = {'anchor_a', 'anchor_b', 'fusion_050'}
    available = {item['job_id']: item for item in items if item.get('status') == 'rendered_unreviewed'}
    if not required.issubset(available):
        # Support other middle values by using first candidate when 0.5 absent.
        candidates = sorted(k for k in available if k.startswith('fusion_'))
        if not {'anchor_a', 'anchor_b'}.issubset(available) or not candidates:
            raise RuntimeError('Need rendered A and B anchor WAVs and at least one Voice C candidate')
        selected = ['anchor_a', 'anchor_b', candidates[0]]
    else:
        selected = ['anchor_a', 'anchor_b', 'fusion_050']
    random.Random(seed).shuffle(selected)
    folder = out / 'blind_listening'
    folder.mkdir(parents=True, exist_ok=False)
    private = {'seed': seed, 'assignment': {}}
    for i, item_id in enumerate(selected, 1):
        name = f'clip_{i:02d}.wav'
        shutil.copy2(available[item_id]['wav'], folder / name)
        private['assignment'][name] = item_id
    write_json(out / 'PRIVATE_answer_key_do_not_share.json', private)
    html = '''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Ghost Voice Blind Listening</title><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font:16px system-ui,sans-serif;background:#12151b;color:#eee;max-width:700px;margin:36px auto;padding:16px}article{padding:20px;border:1px solid #445;border-radius:14px;margin:20px 0}audio{width:100%;margin:10px 0}input{width:100%}label{display:block;margin-top:14px}button{background:#b0f5b7;border:0;padding:13px 18px;border-radius:8px;cursor:pointer}small{color:#aaa}</style>
<h1>Ghost Voice — blind listening</h1><p>Each clip performs the same source melody and words. Please listen on headphones without viewing the private answer key. Rate each clip independently.</p>
<form id="ratings"><div id="clips"></div><label>Comments<textarea name="comments" style="width:100%;height:85px"></textarea></label><p><button type="submit">Download anonymous ratings</button></p></form>
<small>Audio naturalness and difference cannot be determined from embeddings alone. This listening experiment is not a validated perceptual study.</small>
<script>
const container=document.getElementById('clips');
for(let i=1;i<=3;i++){
 const id='clip_'+String(i).padStart(2,'0');const el=document.createElement('article');
 el.innerHTML='<h2>Clip '+i+'</h2><audio controls preload="metadata" src="'+id+'.wav"></audio>'+
 '<label>Naturalness (1 very poor, 5 excellent)<input name="'+id+'_naturalness" type="range" min="1" max="5" value="3"></label>'+
 '<label>Artifacts (1 none, 5 severe)<input name="'+id+'_artifacts" type="range" min="1" max="5" value="3"></label>';
 container.appendChild(el);
}
document.getElementById('ratings').onsubmit=e=>{e.preventDefault();const data=Object.fromEntries(new FormData(e.target));data.date=new Date().toISOString();const blob=new Blob([JSON.stringify(data,null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='ghost_voice_blind_ratings.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)};
</script></html>'''
    (folder / 'index.html').write_text(html, encoding='utf-8')
    manifest['blind_listening_html'] = str(folder / 'index.html')
    manifest['blind_listening_note'] = 'PRIVATE_answer_key_do_not_share.json is OUTSIDE the listening folder. Share only the blind_listening folder.'
    return folder / 'index.html'
