"""Create a Seed-VC-native two-reference style profile."""
from __future__ import annotations
import argparse
import json
import torch
from .seedvc_style import (
    build_fused_style_condition,
    enroll_campplus_identity,
    load_seedvc_campplus,
    save_style_profile,
    blend_campplus_styles,
)


def main() -> None:
    parser = argparse.ArgumentParser(description="Fuse two vocal identities in Seed-VC's CAMPPlus style space.")
    parser.add_argument("--seed-vc-root", required=True, help="Path to a local Plachtaa/seed-vc checkout")
    parser.add_argument("--campplus-checkpoint", required=True, help="Path to funasr/campplus campplus_cn_common.bin")
    parser.add_argument("--a", action="append", required=True, help="Voice A reference WAV; repeat for more takes")
    parser.add_argument("--b", action="append", required=True, help="Voice B reference WAV; repeat for more takes")
    parser.add_argument("--mix-b", type=float, default=0.5, help="Voice B weight, 0=A and 1=B")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--out", default="outputs/ghost_voice_c_seedvc", help="Output prefix")
    args = parser.parse_args()
    if not 0.0 <= args.mix_b <= 1.0:
        parser.error("--mix-b must be between 0 and 1")
    model = load_seedvc_campplus(args.seed_vc_root, args.campplus_checkpoint, args.device)
    a, report_a = enroll_campplus_identity(args.a, model, args.device)
    b, report_b = enroll_campplus_identity(args.b, model, args.device)
    fused, fusion_report = blend_campplus_styles(a, b, args.mix_b)
    report = {"backend": "Seed-VC V1 CAMPPlus", "voice_a": report_a, "voice_b": report_b, "fusion": fusion_report,
              "status": "model_native_style_profile_only_not_audio_rendered",
              "warning": "This profile is ready for the Seed-VC CFM style input, but actual singing output and perceptual novelty still require GPU inference and listening tests."}
    npz_path, json_path = save_style_profile(args.out, fused, report, a, b)
    print(json.dumps({"profile": str(npz_path), "report": str(json_path), "diagnostics": report}, indent=2))


if __name__ == "__main__":
    main()
