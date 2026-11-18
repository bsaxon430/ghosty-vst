"""CLI for reproducible Ghost Voice A/B/C singing experiments."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import torch
from .bench import run_experiment, ensure_backend


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description='Ghost Voice: render A, B and fused-C singing from one held-out performance')
    p.add_argument('--a', action='append', required=True, help='Reference WAV for singer A, repeatable')
    p.add_argument('--b', action='append', required=True, help='Reference WAV for singer B, repeatable')
    p.add_argument('--source', required=True, help='Held-out source singing WAV; its melody and words are retained')
    p.add_argument('--seed-vc-root', required=True, help='Local Seed-VC research checkout already patched by Ghost Voice')
    p.add_argument('--output', default='outputs/ghost_voice_benchmark', help='New, empty directory for experiment artifacts')
    p.add_argument('--mix', action='append', type=float, help='Candidate weight toward B; repeatable (default 0.25, 0.5, 0.75)')
    p.add_argument('--steps', type=int, default=40)
    p.add_argument('--no-fp16', action='store_true', help='Disable half-precision for CPU troubleshooting')
    p.add_argument('--dry-run', action='store_true', help='Validate audio/backend and display commands WITHOUT inference')
    p.add_argument('--pitch-proxy', action='store_true', help='Add slow, experimental YIN pitch agreement statistics')
    p.add_argument('--python', default=sys.executable, help='Python interpreter of the Seed-VC model environment')
    p.add_argument('--config', help='Optional local Seed-VC SVC model configuration')
    p.add_argument('--checkpoint', help='Optional local Seed-VC SVC checkpoint')
    p.add_argument('--timeout-seconds', type=int, default=7200)
    p.add_argument('--rights-confirmed', action='store_true', help='Confirm that these vocals may be used for this experiment')
    return p


def main() -> None:
    p = build_parser()
    args = p.parse_args()
    if not args.rights_confirmed:
        p.error('Add --rights-confirmed after confirming your legal right to process these recordings')
    if not args.dry_run and not torch.cuda.is_available():
        print('WARNING: no local CUDA GPU detected. Seed-VC inference may be slow or unsupported on this machine.', file=sys.stderr)
    manifest = run_experiment(
        a=args.a, b=args.b, source=args.source, seed_root=args.seed_vc_root,
        output=args.output, mixes=args.mix or (0.25, 0.5, 0.75), steps=args.steps,
        fp16=not args.no_fp16, dry_run=args.dry_run, python_executable=args.python,
        checkpoint=args.checkpoint, config=args.config, with_pitch=args.pitch_proxy,
        timeout_seconds=args.timeout_seconds,
    )
    if args.dry_run:
        print(json.dumps(manifest, indent=2))
    else:
        print(json.dumps({'status': manifest['status'],
                          'manifest': str(Path(args.output).resolve() / 'experiment.json'),
                          'blind_listening': manifest['blind_listening_html'],
                          'renders': [{'id': j['job_id'], 'wav': j.get('wav')} for j in manifest['renders']]}, indent=2))


if __name__ == '__main__':
    main()
