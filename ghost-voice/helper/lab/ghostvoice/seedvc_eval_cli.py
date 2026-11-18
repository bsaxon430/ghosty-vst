"""Evaluate a rendered Voice C against its two permitted source identities."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import torch
from .seedvc_style import load_seedvc_campplus
from .seedvc_evaluation import evaluate_voice_c


def main() -> None:
    p = argparse.ArgumentParser(description="CAMPPlus diagnostics for rendered Voice C")
    p.add_argument("--seed-vc-root", required=True)
    p.add_argument("--campplus-checkpoint", required=True)
    p.add_argument("--a", action="append", required=True)
    p.add_argument("--b", action="append", required=True)
    p.add_argument("--c", action="append", required=True, help="Rendered Voice C WAV; repeat for multiple tests")
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--out", default="outputs/voice_c_evaluation.json")
    args = p.parse_args()
    model = load_seedvc_campplus(args.seed_vc_root, args.campplus_checkpoint, args.device)
    report = evaluate_voice_c(args.a, args.b, args.c, model, args.device)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(out), **report}, indent=2))


if __name__ == "__main__":
    main()
